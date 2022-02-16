/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _GNU_SOURCE


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <inttypes.h>
#include "pingpong.h"
#include "client_rc.h"
//#include <ccan/minmax.h>


/// 两种work request id(1号表示是receive,2号表示是send)
enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,
};


/// 定义多个变量和控制参数

static int page_size;      /// 当前系统的page-size
static int use_odp;        /// 是否使用on-deman技术(请求调页技术)
static int implicit_odp;   /// 是否使用隐式on-demand技术
static int prefetch_mr;    /// 是否使用提前获取memory技术
static int use_ts;         /// 是否使用cqe返回带有硬件时间戳的方式(使用的话，程序会记录ts结构体记录整个程序完成的时间状态)
static int validate_buf;   /// 不懂
static int use_dm;		   /// 是否使用device memory
static int use_new_send;   /// 是否启用新的send ,receive函数


/// 一个连接的上下文，包括网卡状态，protect domain ,memory register等等
struct pingpong_context {
	struct ibv_context	*context;      /// 设备上下文
	struct ibv_comp_channel *channel;  /// 事件完成信道(使用notifycation 方式告诉事件完成)
	struct ibv_pd		*pd;           /// 保护域
	struct ibv_mr		*mr;           /// 内存注册结构体(保存已经一个描述已经注册的内存的参数，开始地址，长度,lkey,rkey)
	struct ibv_dm		*dm;           /// device memory 结构体
	union {   						  
		struct ibv_cq		*cq;
		struct ibv_cq_ex	*cq_ex;
	} cq_s;          				   /// compete queue(根据使用情况，选取其中一种)
	struct ibv_qp		*qp;           /// 保存的一对qp
	struct ibv_qp_ex	*qpx;          /// 保存的一对qp extend
	char			*buf;              /// 使用的工作的buffer(上述mr中的信息)，如果使用的是device memory的话这个不用做内存注册,如果普通注册的话，那么本结构体的mr 指向的就是这块内存
	int			 size;                 /// buffer的长度
	int			 send_flags;           /// 指示send属性(可以设置为post send后，为这个work request 产生cqe)
	int			 rx_depth;             /// 设置receive queue的深度(一个下放多少个receive request)
	int			 pending;              /// 待办理的事项(比如已经下发的一个send ,那么标记一下，因为后面需要接受send complete)
	struct ibv_port_attr     portinfo; /// port 属性(ib port)
	uint64_t		 completion_timestamp_mask; /// 和获取的时间戳相关，不知道怎么使用
};


/// 根据使用使用了时间戳，返回一个统一类型的cq(因为使用了时间戳的cq是cqextend类)
static struct ibv_cq *pp_cq(struct pingpong_context *ctx)
{
	return use_ts ? ibv_cq_ex_to_cq(ctx->cq_s.cq_ex) :
		ctx->cq_s.cq;
}

struct pingpong_dest {
	int lid;
	int qpn;
	int psn;
	union ibv_gid gid;
};

/// QP状态的转换，转换成可以发送的状态(qp从init状态知道完成状态的转换)
static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
			  enum ibv_mtu mtu, int sl,
			  struct pingpong_dest *dest, int sgid_idx)
{
	//// psn 需要指定和对方的发送的psn相同
	struct ibv_qp_attr attr = {
		.qp_state		= IBV_QPS_RTR,
		.path_mtu		= mtu,
		.dest_qp_num		= dest->qpn,
		.rq_psn			= dest->psn,
		.max_dest_rd_atomic	= 1,
		.min_rnr_timer		= 12,
		.ah_attr		= {
			.is_global	= 0,
			.dlid		= dest->lid,
			.sl		= sl,
			.src_path_bits	= 0,
			.port_num	= port
		}
	};

	// 如果gid 是一个global 的地址的话
	if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dest->gid;
		attr.ah_attr.grh.sgid_index = sgid_idx;
	}
	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_AV                 |
			  IBV_QP_PATH_MTU           |
			  IBV_QP_DEST_QPN           |
			  IBV_QP_RQ_PSN             |
			  IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER)) {
		fprintf(stderr, "Failed to modify QP to RTR\n");
		return 1;
	}


	/// 将 qp 设置成 rts状态
	attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 14;
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;
	/// 设置自己的发送端的psn (就是自己的psn),而对方指定的发送psn就是我们上述状态转换中的 rq_psn设置dest.psn
	attr.sq_psn	    = my_psn;
	attr.max_rd_atomic  = 1;
	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_TIMEOUT            |
			  IBV_QP_RETRY_CNT          |
			  IBV_QP_RNR_RETRY          |
			  IBV_QP_SQ_PSN             |
			  IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "Failed to modify QP to RTS\n");
		return 1;
	}

	return 0;
}

/// 客户端连接服务端，qp连接的建立
static struct pingpong_dest *pp_client_exch_dest(const char *servername, int port,
						 const struct pingpong_dest *my_dest)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int sockfd = -1;
	struct pingpong_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(servername, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		return NULL;
	}

	gid_to_wire_gid(&my_dest->gid, gid);
	/// 将自己的身份以字符串的方式存在msg中，并且发送到对方
	sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn,
							my_dest->psn, gid);
	if (write(sockfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "Couldn't send local address\n");
		goto out;
	}
	/// 获取对方的身份，存在msg中。
	if (read(sockfd, msg, sizeof msg) != sizeof msg ||
	    write(sockfd, "done", sizeof "done") != sizeof "done") {
		perror("client read/write");
		fprintf(stderr, "Couldn't read/write remote address\n");
		goto out;
	}

	/// 将对方的身份存在rem_dest中
	rem_dest = malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn,
						&rem_dest->psn, gid);
	wire_gid_to_gid(gid, &rem_dest->gid);

out:
	close(sockfd);
	return rem_dest;
}

/// 服务端连接客户端，qp连接的建立（服务端首先在函数中会完成状态的直接到完成状态的转换)
static struct pingpong_dest *pp_server_exch_dest(struct pingpong_context *ctx,
						 int ib_port, enum ibv_mtu mtu,
						 int port, int sl,
						 const struct pingpong_dest *my_dest,
						 int sgid_idx)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_flags    = AI_PASSIVE,
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int sockfd = -1, connfd;
	struct pingpong_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(NULL, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			n = 1;

			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

			if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't listen to port %d\n", port);
		return NULL;
	}

	listen(sockfd, 1);
	connfd = accept(sockfd, NULL, NULL);
	close(sockfd);
	if (connfd < 0) {
		fprintf(stderr, "accept() failed\n");
		return NULL;
	}

	/// 读取客户端发送过来的身份,保存在msg中，然后解析到rem_dest中
	n = read(connfd, msg, sizeof msg);
	if (n != sizeof msg) {
		perror("server read");
		fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
		goto out;
	}

	rem_dest = malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn,
							&rem_dest->psn, gid);
	wire_gid_to_gid(gid, &rem_dest->gid);

	/// 拿到对方的身份就开始了qp连接(qp状态的进一步转换)
	if (pp_connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest,
								sgid_idx)) {
		fprintf(stderr, "Couldn't connect to remote QP\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

	/// 将自己的身份封装在msg中，发送给客户端。
	gid_to_wire_gid(&my_dest->gid, gid);
	sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn,
							my_dest->psn, gid);
	if (write(connfd, msg, sizeof msg) != sizeof msg ||
	    read(connfd, msg, sizeof msg) != sizeof "done") {
		fprintf(stderr, "Couldn't send/recv local address\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}


out:
	close(connfd);
	return rem_dest;
}

/// 根据整个程序输入的各种配置参数返回一个连接的上下文(pingpong_context)
///     (1)完成参数的保存，保存在这个连接的上下文中
///     (2)完成工作块的内存开辟,pd的设定
///     (3)完成qp的创建并且状态转换到init状态
static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, int size,
					    int rx_depth, int port,
					    int use_event)
{
	struct pingpong_context *ctx;

	/// 设置注册内存块的访问权限(首先先添加本地可写,默认含有本地可读)
	int access_flags = IBV_ACCESS_LOCAL_WRITE;

	ctx = calloc(1, sizeof *ctx);
	if (!ctx)
		return NULL;

	/// 指定这个连接的工作内存大小,对本qp中的send request 产生 cqe,记录receive queue 的深度
	ctx->size       = size;
	ctx->send_flags = IBV_SEND_SIGNALED;
	ctx->rx_depth   = rx_depth;

	/// 开辟这样一块用户态内存(这里需要指定对其，因为方便网卡刚好pin住这一页)
	ctx->buf = memalign(page_size, size);
	if (!ctx->buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		goto clean_ctx;
	}

	/// 将我们工作区间的内存内容设置一下
	memset(ctx->buf, 0, size);

	/// 获取设备，保存这个设备的上下文
	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(ib_dev));
		goto clean_buffer;
	}

	/// 如果用户指定了，cqe是按照notifycation的方式返回的话，那么就需要创建一个信道接受notifycation
	if (use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			goto clean_device;
		}
	} else
		ctx->channel = NULL;

	/// 创建一个protect domain
	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		goto clean_comp_channel;
	}

	/// 如果使用了on-demand-page 或者 时间戳 或者 device_memory那么进入下面的特殊情况处理(查看设备是否支持)
	if (use_odp || use_ts || use_dm) {

		/// 创建一个RC 连接支持ODP(SEND,RECV)mask,用于之后的判断
		const uint32_t rc_caps_mask = IBV_ODP_SUPPORT_SEND |
					      IBV_ODP_SUPPORT_RECV;
		struct ibv_device_attr_ex attrx;

		/// 首先先获取设备的能力
		if (ibv_query_device_ex(ctx->context, NULL, &attrx)) {
			fprintf(stderr, "Couldn't query device for its features\n");
			goto clean_pd;
		}

		/// 如果使用了on-demand-page
		if (use_odp) {
			/// 如果查询到的设备不支持ODP  或者 RC不支持SEND,RECV ODP功能,那么报错并且清理开辟的内存并且推出程序
			if (!(attrx.odp_caps.general_caps & IBV_ODP_SUPPORT) ||
			    (attrx.odp_caps.per_transport_caps.rc_odp_caps & rc_caps_mask) != rc_caps_mask) {
				fprintf(stderr, "The device isn't ODP capable or does not support RC send and receive with ODP\n");
				goto clean_pd;
			}
			/// 如果使用了ODP并且使用了隐式odp的话，还需要判断是否支持隐式odp
			if (implicit_odp &&
			    !(attrx.odp_caps.general_caps & IBV_ODP_SUPPORT_IMPLICIT)) {
				fprintf(stderr, "The device doesn't support implicit ODP\n");
				goto clean_pd;
			}

			/// 如果设置了ODP，并且设备也是支持的,那么设置内存注册的access_glags加上IBV_ACCESS_ON_DEMAND
			access_flags |= IBV_ACCESS_ON_DEMAND;
		}

		/// 如果使用了时间戳,那么查看网卡是否具有事件戳的功能
		if (use_ts) {
			if (!attrx.completion_timestamp_mask) {
				fprintf(stderr, "The device isn't completion timestamp capable\n");
				goto clean_pd;
			}
			ctx->completion_timestamp_mask = attrx.completion_timestamp_mask;
		}

		/// 如果指定使用了device memory,那么查看网卡是否支持这个功能，并且还有剩余内存
		if (use_dm) {
			struct ibv_alloc_dm_attr dm_attr = {};

			/// 是否支持device memory
			if (!attrx.max_dm_size) {
				fprintf(stderr, "Device doesn't support dm allocation\n");
				goto clean_pd;
			}

			/// 需求的内存大小是否足够
			if (attrx.max_dm_size < size) {
				fprintf(stderr, "Device memory is insufficient\n");
				goto clean_pd;
			}

			/// 如果支持并且足够的话，那么就开辟device memory指定大下
			dm_attr.length = size;
			ctx->dm = ibv_alloc_dm(ctx->context, &dm_attr);
			if (!ctx->dm) {
				fprintf(stderr, "Dev mem allocation failed\n");
				goto clean_pd;
			}

			/// 不使用指针地址进入注册的内存了，反正是device memory ,使用0作为偏移量
			access_flags |= IBV_ACCESS_ZERO_BASED;
		}
	}

	/// 上面只是对提出的使用方式的检查，下面是对指定方式的设置

	/// 如果使用了implicit-on-demand-page，那么我们注册的内存地址传入NULL(不用自己开辟的那块内存)
	if (implicit_odp) {
		ctx->mr = ibv_reg_mr(ctx->pd, NULL, SIZE_MAX, access_flags);
	/// 如果没有使用implicit-on-demand-page的话，
	/// 判断是否使用了device memory,如果使用的话注册设备上的内存，偏移量0开始。
	/// 否者正常的使用我们之前开辟好的工作空间的内存进行注册
	} else {
		ctx->mr = use_dm ? ibv_reg_dm_mr(ctx->pd, ctx->dm, 0,
						 size, access_flags) :
			ibv_reg_mr(ctx->pd, ctx->buf, size, access_flags);
	}

	if (!ctx->mr) {
		fprintf(stderr, "Couldn't register MR\n");
		goto clean_dm;
	}

	/// 是否启用prefetch_mr，如果是的话，那么提前将workrequest 中的segment_list
	/// 准备好使用ibv_advise_mr调用，使得内核做预处理，优化调用
	if (prefetch_mr) {
		struct ibv_sge sg_list;
		int ret;

		sg_list.lkey = ctx->mr->lkey;
		sg_list.addr = (uintptr_t)ctx->buf;
		sg_list.length = size;

		ret = ibv_advise_mr(ctx->pd, IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE,
				    IB_UVERBS_ADVISE_MR_FLAG_FLUSH,
				    &sg_list, 1);

		if (ret)
			fprintf(stderr, "Couldn't prefetch MR(%d). Continue anyway\n", ret);
	}

	/// 是否使用时间戳，如果指定了。那么使用ibv_cq_init_attr_ex设置cq参数。包括深度，绑定的notifycation 信道，还有wc_flags指定cqe应该返回的参数(这里希望得到tinmstamps)
	if (use_ts) {
		struct ibv_cq_init_attr_ex attr_ex = {
			.cqe = rx_depth + 1,
			.cq_context = NULL,
			.channel = ctx->channel,
			.comp_vector = 0,
			.wc_flags = IBV_WC_EX_WITH_COMPLETION_TIMESTAMP
		};

		ctx->cq_s.cq_ex = ibv_create_cq_ex(ctx->context, &attr_ex);
	/// 如果不需要时间戳的话，那么就是最普通的create函数
	} else {
		ctx->cq_s.cq = ibv_create_cq(ctx->context, rx_depth + 1, NULL,
					     ctx->channel, 0);
	}

	if (!pp_cq(ctx)) {
		fprintf(stderr, "Couldn't create CQ\n");
		goto clean_mr;
	}

	/// 接下来创建qp
	{
		struct ibv_qp_attr attr;

		/// 指定我们创建的qp的各种参数的设置，包括绑定cqueue(需统一格式)，qp中的send_request最大数量，send_request中的最多segment数量等，当然需要指定这个qp的类型（RC这个地方),
		struct ibv_qp_init_attr init_attr = {
			.send_cq = pp_cq(ctx),
			.recv_cq = pp_cq(ctx),
			.cap     = {
				.max_send_wr  = MAX_SEND_WR,
				.max_recv_wr  = rx_depth,
				.max_send_sge = 1,
				.max_recv_sge = 1
			},
			.qp_type = IBV_QPT_RC
		};

		/// 是否使用新版本的send ,receive函数(创建qp_extend)
		if (use_new_send) {
			struct ibv_qp_init_attr_ex init_attr_ex = {};

			init_attr_ex.send_cq = pp_cq(ctx);
			init_attr_ex.recv_cq = pp_cq(ctx);
			init_attr_ex.cap.max_send_wr = 1;
			init_attr_ex.cap.max_recv_wr = rx_depth;
			init_attr_ex.cap.max_send_sge = 1;
			init_attr_ex.cap.max_recv_sge = 1;
			init_attr_ex.qp_type = IBV_QPT_RC;

			init_attr_ex.comp_mask |= IBV_QP_INIT_ATTR_PD |
						  IBV_QP_INIT_ATTR_SEND_OPS_FLAGS;
			init_attr_ex.pd = ctx->pd;
			init_attr_ex.send_ops_flags = IBV_QP_EX_WITH_SEND;

			ctx->qp = ibv_create_qp_ex(ctx->context, &init_attr_ex);

		/// 创建新版本的qp
		} else {
			ctx->qp = ibv_create_qp(ctx->pd, &init_attr);
		}

		if (!ctx->qp)  {
			fprintf(stderr, "Couldn't create QP\n");
			goto clean_cq;
		}

		/// 需要对新版本的qp,那么需要将ibv_create_qp_ex创建的普通类型的qp转换成qpextend类型， 并且保存在qpx中
		if (use_new_send)
			ctx->qpx = ibv_qp_to_qp_ex(ctx->qp);

		/// 上面只是按照要求创建了qp,对于实际的qp，还有一些属性是需要我们查询的
		/// IBV_QP_CAP指定我们想要查询的字段，attr是我们是指定查询的字段，而init_attr是所有返回的查询的字段.
		/// 如果一个sendqueue 能表示的数据大于我们的 sigsize 的话,我们开启inline 数据模式(离散地址，组合连续内存)
		ibv_query_qp(ctx->qp, &attr, IBV_QP_CAP, &init_attr);
		if (init_attr.cap.max_inline_data >= size && !use_dm)
			ctx->send_flags |= IBV_SEND_INLINE;
	}

	{
		/// 修改得到的qp装填到init状态(attr是要设置的值是多少，下面的或掩码是指示修改那几个属性)
		/// 其中的pkey_index 不懂，qp_access_flags就是内存注册对应的那个flag。权限不能高于内存注册的权限。
		struct ibv_qp_attr attr = {
			.qp_state        = IBV_QPS_INIT,
			.pkey_index      = 0,
			.port_num        = port,
			.qp_access_flags = 0
		};

		if (ibv_modify_qp(ctx->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify QP to INIT\n");
			goto clean_qp;
		}
	}

	return ctx;

clean_qp:
	ibv_destroy_qp(ctx->qp);

clean_cq:
	ibv_destroy_cq(pp_cq(ctx));

clean_mr:
	ibv_dereg_mr(ctx->mr);

clean_dm:
	if (ctx->dm)
		ibv_free_dm(ctx->dm);

clean_pd:
	ibv_dealloc_pd(ctx->pd);

clean_comp_channel:
	if (ctx->channel)
		ibv_destroy_comp_channel(ctx->channel);

clean_device:
	ibv_close_device(ctx->context);

clean_buffer:
	free(ctx->buf);

clean_ctx:
	free(ctx);

	return NULL;
}

/// 对连接上下文的资源的释放
static int pp_close_ctx(struct pingpong_context *ctx)
{
	if (ibv_destroy_qp(ctx->qp)) {
		fprintf(stderr, "Couldn't destroy QP\n");
		return 1;
	}

	if (ibv_destroy_cq(pp_cq(ctx))) {
		fprintf(stderr, "Couldn't destroy CQ\n");
		return 1;
	}

	if (ibv_dereg_mr(ctx->mr)) {
		fprintf(stderr, "Couldn't deregister MR\n");
		return 1;
	}

	if (ctx->dm) {
		if (ibv_free_dm(ctx->dm)) {
			fprintf(stderr, "Couldn't free DM\n");
			return 1;
		}
	}

	if (ibv_dealloc_pd(ctx->pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ctx->channel) {
		if (ibv_destroy_comp_channel(ctx->channel)) {
			fprintf(stderr, "Couldn't destroy completion channel\n");
			return 1;
		}
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}
	for(int i =0;i<MAX_RECEIVE_WR;i++)
	{
		if(receive_array[i] != NULL)
		{
			free(receive_array[i]);
			receive_array[i] = NULL;
		}
	}

	free(ctx->buf);
	free(ctx);

	return 0;
}

/// 完成receive post的补充
static int pp_post_recv(struct pingpong_context *ctx)
{
	//printf("post recv post....\n");
	int i;
	for(i=0;i<MAX_RECEIVE_WR;i++)
	{
		// 如果是空的话就补上内存块，并且下发下去
		if(receive_array[i] == NULL)
		{
			receive_array[i] = (char *)malloc(MAX_SEGMENT_SIZE);

			struct ibv_sge list = {
				.addr	= use_dm ? 0 : (uintptr_t) receive_array[i],
				.length = MAX_SEGMENT_SIZE,
				.lkey	= ctx->mr->lkey
			};
			struct ibv_recv_wr wr = {
				.wr_id	    = i,
				.sg_list    = &list,
				.num_sge    = 1,
			};
			struct ibv_recv_wr *bad_wr;
			if (ibv_post_recv(ctx->qp, &wr, &bad_wr))
			{
				printf("can not post recv request\n");
			}
			//printf("post recv post at index %d\n", i);
		}	
	}
	return 0;
}


static int pp_post_send(struct pingpong_context *ctx, char * message,uint32_t message_size)
{
	struct ibv_sge list = {
		.addr	= use_dm ? 0 : (uintptr_t) message,
		.length = message_size,
		.lkey	= ctx->mr->lkey
	};
	/// 设置post send event的workrequest id （之后再cqe中可获取完成对应的这个id)
	struct ibv_send_wr wr = {
		.wr_id	    = 100,
		.sg_list    = &list,
		.num_sge    = 1,
		.opcode     = IBV_WR_SEND,
		.send_flags = ctx->send_flags,
	};
	struct ibv_send_wr *bad_wr;

	return ibv_post_send(ctx->qp, &wr, &bad_wr);
	
}



struct ts_params {
	uint64_t		 comp_recv_max_time_delta;
	uint64_t		 comp_recv_min_time_delta;
	uint64_t		 comp_recv_total_time_delta;
	uint64_t		 comp_recv_prev_time;
	int			 last_comp_with_ts;
	unsigned int		 comp_with_time_iters;
};

/// 传入整体的上下文，send 完成的计次，receive 完成的计次，post receive_request的个数，迭代次数，cqe对应的workrequestid,状态,cque完成的时间
/// 对本次cqe的处理函数，更新ts 结构体(体现服务器端的receives 时间属性), 如果没有receive wr了，就再开指定那么多
/// 如果是send 就记录一下收到的次数。并且再次下放一个send request
static inline int parse_single_wc(struct pingpong_context *ctx, int *scnt,
				  int *rcnt, int *routs, int iters,
				  uint64_t wr_id, enum ibv_wc_status status,
				  uint32_t byte_len,
				  uint64_t completion_timestamp,
				  struct ts_params *ts)
{
	
	// if (status != IBV_WC_SUCCESS) {
	// 	fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
	// 		ibv_wc_status_str(status),
	// 		status, (int)wr_id);
	// 	return 1;
	// }

	// // 查看本次wqe 的id值，如果是send 的话，那么就将scnt 完成计数器++, 如果是receive的话，recv 完成计数器++,并且判断剩余处理的receive request 减少到0？
	// // 如果receive request 没有了的话，那么就再添加足够多的post request 到recv 队列中
	// switch ((int)wr_id) {
	// case PINGPONG_SEND_WRID:
	// 	printf("send the %d bytes as %s to server \n",byte_len,ctx->buf);
	// 	++(*scnt);
	// 	break;

	// case PINGPONG_RECV_WRID:
	// 	if (--(*routs) <= 1) {
	// 		*routs += pp_post_recv(ctx, ctx->rx_depth - *routs);
	// 		if (*routs < ctx->rx_depth) {
	// 			fprintf(stderr,
	// 				"Couldn't post receive (%d)\n",
	// 				*routs);
	// 			return 1;
	// 		}
	// 	}
	// 	++(*rcnt);
	// 	ts->last_comp_with_ts = 0;

		
	// 	// // get the ack message then post one send
	// 	// if (tj_pp_post_send(ctx,"nihao")) {
	// 	// 	fprintf(stderr, "Couldn't post send\n");
	// 	// 	return 1;
	// 	// }

	// 	break;

	// default:
	// 	fprintf(stderr, "Completion for unknown wr_id %d\n",
	// 		(int)wr_id);
	// 	return 1;
	// }

	

	return 0;
}



//// 打印出本程序的使用方法
static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -p, --port=<port>      listen on/connect to port <port> (default 18515)\n");
	printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
	printf("  -i, --ib-port=<port>   use port <port> of IB device (default 1)\n");
	printf("  -s, --size=<size>      size of message to exchange (default 4096)\n");
	printf("  -m, --mtu=<size>       path MTU (default 1024)\n");
	printf("  -r, --rx-depth=<dep>   number of receives to post at a time (default 500)\n");
	printf("  -n, --iters=<iters>    number of exchanges (default 1000)\n");
	printf("  -l, --sl=<sl>          service level value\n");
	printf("  -e, --events           sleep on CQ events (default poll)\n");
	printf("  -g, --gid-idx=<gid index> local port gid index\n");
	printf("  -o, --odp		    use on demand paging\n");
	printf("  -O, --iodp		    use implicit on demand paging\n");
	printf("  -P, --prefetch	    prefetch an ODP MR\n");
	printf("  -t, --ts	            get CQE with timestamp\n");
	printf("  -c, --chk	            validate received buffer\n");
	printf("  -j, --dm	            use device memory\n");
	printf("  -N, --new_send            use new post send WR API\n");
}


struct ibv_device      **dev_list;
struct ibv_device	*ib_dev;
struct pingpong_context *ctx;
struct pingpong_dest     my_dest;     		   /// 连接的地址(其中包括local id,gloabal id,qpnumber, packet seq number),这个也是需要两端相互交换的东西
struct pingpong_dest    *rem_dest;             /// 保存的对端的pingpone_dest
struct timeval           start, end;
char                    *ib_devname = NULL;
char                    *servername = NULL;    /// 对于客户端需要保存服务器的ip
unsigned int             port = 18515;         /// tcp 交换信息使用的ip地址
int                      ib_port = 1;    	   /// ib端口
unsigned int             size = 4096;          /// 开辟工作空间的内存size(一般开辟一个工作空间大小是一个页大小)
enum ibv_mtu		 mtu = IBV_MTU_1024;       
unsigned int             rx_depth = 500;       /// 设置receive queue的深度(一次下发的receive request个数)
unsigned int             iters = 1;         /// 算法两端发送，接受的迭代次数
int                      use_event = 0;		   /// 是否使用notifycation 通知cqe
int                      routs;                /// 当前的receive queue的剩余未处理个数(receive outstanding)
int                      rcnt, scnt;           /// receive 个数计数器, sendcount个数计数器(两种post的完成数量)
int                      num_cq_events = 0;    /// 统计使用notifycation通知的cqe个数(用于最后的确认收到notifycation)
int                      sl = 0;               /// service level(服务类型)
int			 gidx = -1;                        /// gidindex ,如果是有全局路由的话，需要这个(全局表的第几项)
char			 gid[33];                      /// gid 字符串
struct ts_params	 ts;   				       /// 时间记录(如果使用时间戳，那么时间信息保存在这个结构体中)






/// 程序的主要逻辑是两个server,一个是客户端，一个是服务端，都创建一对qp，使用tcp连接qp。
/// qp 只使用一个工作注册的内存块(一个页的大小)。发送端每次下发一个send request(发送自己的内存块的内容，存在一个segment中)
/// 接受端收到后转存到自己的工作注册内存块（也是一个页的大小)。发送端生成cqe后再次下发send request,接收端生成cqe后判断，receive request
/// 剩余个数，如果不足再一次性添加到深度个数（不足)。
int dpu_client_init(char * ip_out,unsigned port_out)
{
	int argc;
	char * *argv = NULL;
	
    char * parameters[2];
    parameters[0] = "client";
    parameters[1] = ip_out;
	argv = parameters;

	argc = 2;
	port = port_out;
	use_odp = 1;
	implicit_odp = 1;
	
	srand48(getpid() * time(NULL));                /// 不懂为什么有个这个存在



	/// 下列代码是读取程序的参数，上述的设置都是可以通过程序的跟随参数修改而来。如果不使用的话，就使用上述的默认值
	while (1) {
		int c;

		static struct option long_options[] = {
			{ .name = "port",     .has_arg = 1, .val = 'p' },
			{ .name = "ib-dev",   .has_arg = 1, .val = 'd' },
			{ .name = "ib-port",  .has_arg = 1, .val = 'i' },
			{ .name = "size",     .has_arg = 1, .val = 's' },
			{ .name = "mtu",      .has_arg = 1, .val = 'm' },
			{ .name = "rx-depth", .has_arg = 1, .val = 'r' },
			{ .name = "iters",    .has_arg = 1, .val = 'n' },
			{ .name = "sl",       .has_arg = 1, .val = 'l' },
			{ .name = "events",   .has_arg = 0, .val = 'e' },
			{ .name = "gid-idx",  .has_arg = 1, .val = 'g' },
			{ .name = "odp",      .has_arg = 0, .val = 'o' },
			{ .name = "iodp",     .has_arg = 0, .val = 'O' },
			{ .name = "prefetch", .has_arg = 0, .val = 'P' },
			{ .name = "ts",       .has_arg = 0, .val = 't' },
			{ .name = "chk",      .has_arg = 0, .val = 'c' },
			{ .name = "dm",       .has_arg = 0, .val = 'j' },
			{ .name = "new_send", .has_arg = 0, .val = 'N' },
			{}
		};

		c = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:eg:oOPtcjN",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'p':
			port = strtoul(optarg, NULL, 0);
			if (port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'd':
			ib_devname = strdupa(optarg);
			break;

		case 'i':
			ib_port = strtol(optarg, NULL, 0);
			if (ib_port < 1) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 's':
			size = strtoul(optarg, NULL, 0);
			break;

		case 'm':
			mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
			if (mtu == 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'r':
			rx_depth = strtoul(optarg, NULL, 0);
			break;

		case 'n':
			iters = strtoul(optarg, NULL, 0);
			break;

		case 'l':
			sl = strtol(optarg, NULL, 0);
			break;

		case 'e':
			++use_event;
			break;

		case 'g':
			gidx = strtol(optarg, NULL, 0);
			break;

		case 'o':
			use_odp = 1;
			break;
		case 'P':
			prefetch_mr = 1;
			break;
		case 'O':
			use_odp = 1;
			implicit_odp = 1;
			break;
		case 't':
			use_ts = 1;
			break;
		case 'c':
			validate_buf = 1;
			break;

		case 'j':
			use_dm = 1;
			break;

		case 'N':
			use_new_send = 1;
			break;

		default:
			usage(argv[0]);
			return 1;
		}
	}

	/// 对于客户端，至少需要一个ip地址(服务器的ip地址)
	if (optind == argc - 1)
		servername = strdupa(argv[optind]);
	else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	/// 对于device 的内存就不能使用 页请求了吧。
	if (use_odp && use_dm) {
		fprintf(stderr, "DM memory region can't be on demand\n");
		return 1;
	}

	/// 对于prefetch_mr，只是在on-deman meory中使用
	if (!use_odp && prefetch_mr) {
		fprintf(stderr, "prefetch is valid only with on-demand memory region\n");
		return 1;
	}

	/// 如果要记录时间的话，先对ts结构体，重置一下(workrequest的完成时间啊，完成时间间隔之类的统计)。
	if (use_ts) {
		ts.comp_recv_max_time_delta = 0;
		ts.comp_recv_min_time_delta = 0xffffffff;
		ts.comp_recv_total_time_delta = 0;
		ts.comp_recv_prev_time = 0;
		ts.last_comp_with_ts = 0;
		ts.comp_with_time_iters = 0;
	}

	/// 得到系统的页大小
	page_size = sysconf(_SC_PAGESIZE);

	/// 获取当前节点的ib 设备list
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return 1;
	}

	/// 如果没有指定ib_device的话，就选用第一个ib_device
	if (!ib_devname) {
		ib_dev = *dev_list;
		if (!ib_dev) {
			fprintf(stderr, "No IB devices found\n");
			return 1;
		}
	/// 否者的话就去遍历你指定的ib_device
	} else {
		int i;
		for (i = 0; dev_list[i]; ++i)
			if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
				break;
		ib_dev = dev_list[i];
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", ib_devname);
			return 1;
		}
	}

	/// 初始化我们的整个连接的结构体
	ctx = pp_init_ctx(ib_dev, size, rx_depth, ib_port, use_event);
	if (!ctx)
		return 1;

	/// 完成max_receive个 receive requet的下发(因为在pp_init_ctx中的qp就是已经可以下放recv wr的了)
	for(int i = 0;i< MAX_RECEIVE_WR;i++)
	{
		receive_array[i] = NULL;
	}
	if(pp_post_recv(ctx))
	{
		fprintf(stderr, "Couldn't post receive (%d)\n", MAX_RECEIVE_WR);
		return 1;
	}
	
	/// 如果使用notify 的方式反馈，那么这个地方需要开启
	if (use_event)
		/// 选择性的notify(可以设置为只对某些标签的wr产生通知，这里表示全部都产生notify)
		if (ibv_req_notify_cq(pp_cq(ctx), 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}

	/// ibv_query_port 的直接调用，将port的信息存储在portinfo里面
	if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
		fprintf(stderr, "Couldn't get port info\n");
		return 1;
	}

	/// 够造自己的连接身份(my_dest,rm_dest是连接者对方的身份)
	/// lid的构造
	my_dest.lid = ctx->portinfo.lid;
	if (ctx->portinfo.link_layer != IBV_LINK_LAYER_ETHERNET &&
							!my_dest.lid) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	/// gid的构造,如果指定了gidx,那么获取自己的gidx。（否则置0）
	if (gidx >= 0) {
		if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
			fprintf(stderr, "can't read sgid of index %d\n", gidx);
			return 1;
		}
	} else
		memset(&my_dest.gid, 0, sizeof my_dest.gid);

	/// qpn的构造
	my_dest.qpn = ctx->qp->qp_num;
	/// psn(packet sequent number)构造(这里只是模拟连接，所以选用一个随机数)
	my_dest.psn = lrand48() & 0xffffff;
	/// 将gid 到字符串的转换函数
	inet_ntop(AF_INET6, &my_dest.gid, gid, sizeof gid);
	/// 自己身份的填写完成，之后的打印调试
	printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
	       my_dest.lid, my_dest.qpn, my_dest.psn, gid);


	/// 如果指定了servername,那么自己就是一个客户端，否则是一个服务器端。如下是两个
	/// node之间的信息的交换(my_dest,rm_dest的交换),逻辑上就是qp的连接。主注意的是client excan中qp不改变状态
	/// 但是服务器端会将qp置为完成状态
	if (servername)
		rem_dest = pp_client_exch_dest(servername, port, &my_dest);
	else
		rem_dest = pp_server_exch_dest(ctx, ib_port, mtu, port, sl,
								&my_dest, gidx);

	/// 打印得到的对方身份
	if (!rem_dest)
		return 1;

	inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);
	printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
	       rem_dest->lid, rem_dest->qpn, rem_dest->psn, gid);

	/// 如果是clinet，这个时候完成qp状态的转变(最终的完成状态)
	if (servername)
		if (pp_connect_ctx(ctx, ib_port, my_dest.psn, mtu, sl, rem_dest,
					gidx))
			return 1;

	/// 并且设置了上述之后，现在不管客户端，还是服务端都应该需要处理recv cqe了。(因为之前下发过recv wr)
	ctx->pending = PINGPONG_RECV_WRID;

	ctx->buf[0]='h';
	
	/// 如果是客户端的话，那么可以下发一个 send request了
	if (servername) {
		/// 不懂这代码作什么
		if (validate_buf)
			for (int i = 0; i < size; i += page_size)
				ctx->buf[i] = i / page_size % sizeof(char);
	}

	return 0;
}

int dpu_client_try_recv(char ** message, uint32_t * get_lenth)
{
	int ret;
	int ne;
	struct ibv_wc wc[2];
	/// 轮询的方式在这个地方一直获取cqe, 直到获取到recv的cqe
	ne = ibv_poll_cq(pp_cq(ctx), 1, wc);
	if (ne < 0) {
		fprintf(stderr, "poll CQ failed %d\n", ne);
		return 1;
	}
	//如果有了一个cqe
	if(ne == 1)
	{
		// 判断这个cqe的状态
		if (wc[0].status != IBV_WC_SUCCESS) {
			fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
			ibv_wc_status_str(wc[0].status),
			wc[0].status, (int)wc[0].wr_id);
			return 1;
		}

		// 如果这个cqe是recv
		if( wc[0].wr_id < 100)
		{
			// 将地址传出去 和接收到的长度
			*message = receive_array[wc[0].wr_id];
			*get_lenth = wc[0].byte_len;
			// 将当前的array设置为0，并且添加recv_buf.
			receive_array[wc[0].wr_id] = NULL;
			// 补充post
			if(pp_post_recv(ctx))
			{
				printf("can not post recv request\n");
			}
			return 0;
		}
		// 如果是send &  free version的话
		else if((wc[0].wr_id) > 100){
			//printf(" come to free %ld",(wc[0].wr_id));
			free((void*)(wc[0].wr_id));
		}
	}
	// 如果没有cqe 或者是send的cqe
	*message = NULL;
	*get_lenth = 0;
	return 0;
}

int dpu_client_recv(char **message, uint32_t * get_lenth)
{
	
	int ret;
	int ne;
	struct ibv_wc wc[2];
	/// 轮询的方式在这个地方一直获取cqe, 直到获取到recv的cqe
	do {
		ne = ibv_poll_cq(pp_cq(ctx), 1, wc);
		if (ne < 0) {
			fprintf(stderr, "poll CQ failed %d\n", ne);
			return 1;
		}
		//如果有了一个cqe
		if(ne == 1)
		{
			// 判断这个cqe的状态
			if (wc[0].status != IBV_WC_SUCCESS) {
				fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
				ibv_wc_status_str(wc[0].status),
				wc[0].status, (int)wc[0].wr_id);
				return 1;
			}

			// 如果这个cqe是recv
			if( wc[0].wr_id < 100)
			{
				// 将地址传出去 和接收到的长度
				*message = receive_array[wc[0].wr_id];
				*get_lenth = wc[0].byte_len;
				// 将当前的array设置为0，并且添加recv_buf.
				receive_array[wc[0].wr_id] = NULL;
				// 补充post
				if(pp_post_recv(ctx))
				{
					printf("can not post recv request\n");
				}
				return 0;
			}
			// 如果是send &  free version的话
			else if((wc[0].wr_id) > 100){
				//printf(" come to free %ld",(wc[0].wr_id));
				free((void*)(wc[0].wr_id));
			}
		}
	} while (1);
	return 0;

}
int dpu_client_send(char *message, uint32_t send_size)
{
	if(send_size > MAX_SEGMENT_SIZE)
	{
		printf("the message size is too big\n");
		return 1;
	}
	return pp_post_send(ctx, message,send_size);
}
int dpu_client_send_free(char *message, uint32_t send_size)
{
	// 判断是否过大
	if(send_size > MAX_SEGMENT_SIZE)
	{
		printf("the message size is too big\n");
		return 1;
	}

	// post send
	struct ibv_sge list = {
		.addr	= use_dm ? 0 : (uintptr_t) message,
		.length = send_size,
		.lkey	= ctx->mr->lkey
	};
	/// 设置设置地址用于之后的free
	struct ibv_send_wr wr = {
		.wr_id	    = (uint64_t)message,
		.sg_list    = &list,
		.num_sge    = 1,
		.opcode     = IBV_WR_SEND,
		.send_flags = ctx->send_flags,
	};
	struct ibv_send_wr *bad_wr;
	return ibv_post_send(ctx->qp, &wr, &bad_wr);
}


int dpu_client_clean()
{
	
	{
		/// 服务器端并且validate_buf
		if ((!servername) && (validate_buf)) {
			/// 如果使用了device memory那么我们还需要把device memeory 拷贝到host 内存中
			if (use_dm)
				if (ibv_memcpy_from_dm(ctx->buf, ctx->dm, 0, size)) {
					fprintf(stderr, "Copy from DM buffer failed\n");
					return 1;
				}
			for (int i = 0; i < size; i += page_size)
				if (ctx->buf[i] != i / page_size % sizeof(char))
					printf("invalid data in page %d\n",
					       i / page_size);
		}
	}
	printf("client down!!!\n");
	/// 设置在notify阶段，我们依据确认收到了多少个notifycation。程序可以ibv_destroy_cq了(不然的destory_cq会阻塞等待)。
	ibv_ack_cq_events(pp_cq(ctx), num_cq_events);

	if (pp_close_ctx(ctx))
		return 1;

	ibv_free_device_list(dev_list);
	free(rem_dest);

	return 0;
}




