#include "my_mpi_op.h"

struct task_record
{
    uint32_t task_id;  // 飞行的taskid
    char busy;   // 是否在飞行
    uint32_t recv_byte; // 任务已经收到的字节数
    char * recv_buf;    // 接受缓存的头指针
    uint32_t need_recv_byte; //任务需要收到的字节数
};

// 定义飞行的任务数量
struct task_record my_task[MAX_SEND_WR];
uint32_t inflight_mark;

int TYPE_to_SIZE(MPI_Datatype data_type)
{
    if(data_type == MPI_CHAR){
        return 1;
    }
    if(data_type == MPI_SHORT){
        return 2;
    }
    if(data_type == MPI_INT){
        return 4;
    }
    if(data_type == MPI_LONG){
        return 4;
    }
    if(data_type == MPI_UNSIGNED_CHAR){
        return 1;
    }
    if(data_type == MPI_UNSIGNED_SHORT){
        return 2;
    }
    if(data_type == MPI_UNSIGNED){
        return 4;
    }
    if(data_type == MPI_UNSIGNED_LONG){
        return 4;
    }
    if(data_type == MPI_FLOAT){
        return 8;
    }
    if(data_type == MPI_DOUBLE){
        return 8;
    }
    if(data_type == MPI_LONG_DOUBLE){
        return 16;
    }
    if(data_type == MPI_BYTE){
        return 1;
    }
    if(data_type == MPI_PACKED){
        return 0;
    }
    printf("type to size error\n");
    return 13;
}

uint32_t MPI_to_DPU(MPI_Datatype data_type)
{
    if(data_type == MPI_CHAR)
    {
        return 0;
    }
    if(data_type == MPI_SHORT)
    {
        return 1;
    }
    if(data_type == MPI_INT)
    {
        return 2;
    }
    if(data_type == MPI_LONG)
    {
        return 3;
    }
    if(data_type == MPI_UNSIGNED_CHAR)
    {
        return 4;
    }
    if(data_type == MPI_UNSIGNED_SHORT)
    {
        return 5;
    }
    if(data_type == MPI_UNSIGNED)
    {
        return 6;
    }
    if(data_type == MPI_UNSIGNED_LONG)
    {
        return 7;
    }
    if(data_type == MPI_FLOAT)
    {
        return 8;
    }
    if(data_type == MPI_DOUBLE)
    {
        return 9;
    }
    if(data_type == MPI_LONG_DOUBLE)
    {
        return 10;
    }
    if(data_type == MPI_BYTE)
    {
        return 11;
    }
    if(data_type == MPI_PACKED)
    {
        return 12;
    }
    printf("MPI to DPU error \n");
    return 13;

}

MPI_Datatype DPU_to_MPI(uint32_t dpu_type)
{
    switch (dpu_type){
        case 0:
            return MPI_CHAR;
        case 1:
            return MPI_SHORT;
        case 2:
            return MPI_INT;
        case 3:
            return MPI_LONG;
        case 4:
            return MPI_UNSIGNED_CHAR;
        case 5:
            return MPI_UNSIGNED_SHORT;
        case 6:
            return MPI_UNSIGNED;
        case 7:
            return MPI_UNSIGNED_LONG;
        case 8:
            return MPI_FLOAT;
        case 9:
            return MPI_DOUBLE;
        case 10:
            return MPI_LONG_DOUBLE;
        case 11:
            return MPI_BYTE;
        case 12:
            return MPI_PACKED;
        default:
            printf("DPU to MPI error\n");
			return NULL;
	}
}

// 组合数据包
int pack_parameters(char * buf,uint32_t total_lenth,uint32_t id,uint32_t inner_id, uint32_t each_process, MPI_Datatype data_type)
{
    char * begin_addr =  buf + total_lenth - PARA_SIZE;
    ((uint32_t *)begin_addr)[0] = id;
    ((uint32_t *)begin_addr)[1] = inner_id;
    ((uint32_t *)begin_addr)[2] = each_process;
    ((uint32_t *)begin_addr)[3] = MPI_to_DPU(data_type);
    return 0;
}
// 解析数据包
int unpack_parameters(char * recv_data,uint32_t total_lenth, uint32_t *id, uint32_t *inner_id)
{
    char * begin_addr = recv_data + total_lenth - PARA_SIZE;
    *id = ((uint32_t *)begin_addr)[0];
    *inner_id = ((uint32_t *)begin_addr)[1];
    return 0;
}

// 初始化
int my_mpi_init()
{
    // 获取全局的rank
    MPI_Comm_rank(MPI_COMM_WORLD, &my_world_rank);
    // 获取处理器名字
    int name_length;
    int fix_name_length = 100;
    my_processorName = (char*)malloc(fix_name_length);
    memset(my_processorName, 0, 100);
    MPI_Get_processor_name(my_processorName, &name_length);
    // 获取size
    MPI_Comm_size(MPI_COMM_WORLD, &my_world_size);
    // 获取local rank
    int my_rank = my_world_rank;
    
    char* processorName = (char*)malloc(fix_name_length);
    memset(processorName, 0, fix_name_length);
    MPI_Get_processor_name(processorName, &name_length);
   
    //printf("process name:%s rank:%d\n", processorName, my_rank);
    int size = my_world_size;
    char* processNameRecvBuff = (char*)malloc(fix_name_length*size);
    memset(processNameRecvBuff, 0, fix_name_length*size);
    memcpy(&processNameRecvBuff[my_rank*fix_name_length], processorName, fix_name_length);
    // collect hostnames, form comma separated list
    MPI_Allgather(processorName, fix_name_length, MPI_CHAR, processNameRecvBuff, fix_name_length, MPI_CHAR, MPI_COMM_WORLD);
    //printf("mpi all reduce finished\n");
    for(int i = 0;  i < size; i++){
        processNameRecvBuff[fix_name_length*i+fix_name_length-1] = '\0';
        //printf("rank%d: process name:%s\n", i, &processNameRecvBuff[fix_name_length*i]);
    }
    int localRank = 0;
    for(int i = 0;  i < my_rank; i++){
        if(strcmp(processorName, &processNameRecvBuff[fix_name_length*i]) == 0){
                localRank += 1;
        }
    }
    //printf("localRank of process:%d is %d", my_rank, localRank);
    my_local_rank = localRank;

    // 获取dpu 的ip 并且完成连接
    //for each host, find its corresponding dpu's IP/name
	//for example HOST thor009 <-> thor_bf09 DPU
    char dpu_name[100];
    memset(dpu_name,0,100);
    dpu_name[0]='t';
    dpu_name[1]='h';
    dpu_name[2]='o';
    dpu_name[3]='r';
    dpu_name[4]='-';
    dpu_name[5]='b';
    dpu_name[6]='f';
    dpu_name[7]=processorName[5];
    dpu_name[8]=processorName[6];
    dpu_client_init(dpu_name,8888+localRank);
    //初始化
    memset(my_task,0,sizeof(struct task_record)* MAX_SEND_WR);
    inflight_mark =0;
    return 0;
}


int add_one_task(uint32_t task_id, char * recv_buf, uint32_t need_recv_byte)
{
    for(int i = 0; i< MAX_SEND_WR;i++)
    {
        // 如果是空闲的
        if(my_task[i].busy == 0)
        {
            my_task[i].task_id =task_id;
            my_task[i].busy = 1;
            my_task[i].recv_byte = 0;
            my_task[i].recv_buf = recv_buf;
            my_task[i].need_recv_byte = need_recv_byte;
            return 0;
        }
    }
    printf("can not find empty slot\n");
    return 1;
}

int check_slot_empty()
{
    for(int i = 0; i< MAX_SEND_WR;i++)
    {
        // 如果是空闲的
        if(my_task[i].busy == 0)
        {
            return 0;
        }
    }
    return 1;
}
// 
// 
int my_mpi_ialltoall(const void *sendbuf, int sendcount,
            MPI_Datatype sendtype, void *recvbuf, int recvcount,
            MPI_Datatype recvtype, uint32_t task_id,uint32_t send_lenth)
{
    //如果没有空闲的slot，说明on-the-fly的包数量达到了上限，用户此次调用需要等
    if(check_slot_empty() == 1) {
        //printf("the number of on-the-fly packet has reached its maxlimit, so client should wait until some missions completed\n");
        return 1;
    }

    int type = MPI_to_DPU(sendtype);
	int type_size = TYPE_to_SIZE(sendtype);
	int block_size = sendcount * type_size;
    int data_size = send_lenth;
	if(block_size * my_world_size != data_size) {
		printf("error: data_size input wrong\n");
	}
    int send_segment_id = 0;

	////////////////////////////////////////////////////////////////////////
    //如果客户数据大于一次最多传输的数据，需要切分segment并shuffle
	//do segment shuffle if data size > MAX_SEGMENT_SIZE
	//segment shuffle process:
	int maxmsg_size = MAX_SEGMENT_SIZE - PARA_SIZE;
	if(data_size > maxmsg_size) {
		//printf("SHUFFLE for host %d mission %d, alltoall with data size(%d) > MAX message size, do segment shuffle!\n",my_world_rank, task_id, data_size);
		int segment_number = 0;
		int segment_size = 0;
		segment_number = data_size / maxmsg_size;
		if(data_size % maxmsg_size == 0) {
		}
		else {
			segment_number++;
		}
		segment_size = data_size / segment_number;
		int small_block_size = block_size / segment_number;
		if(segment_number * maxmsg_size < data_size) {
			printf("Error:segment number %d, maxmsgsize %d, data size %d exit\n", segment_number, maxmsg_size, data_size);
			printf("please make sure the data size can be divided by the number of hosts/DPUs\n");
			return 1;
		}
		else {
			//printf("SHUFFLE for host %d mission %d, data size(%d) split into %d segments, segment size %d, block size %d, small block size %d\n",my_world_rank, task_id, data_size, segment_number, segment_size, block_size, small_block_size);
		}
		if(MAX_SEND_WR - inflight_mark < segment_number)
		{
			return 1;
		}
		
		for(int i=0; i<segment_number; i++) {
			send_segment_id = i;
            char * send_buffer = (char*)malloc(maxmsg_size+PARA_SIZE);
			for(int j=0; j<my_world_size; j++) {
				memcpy(send_buffer+j*small_block_size, sendbuf+j*block_size+i*small_block_size, small_block_size);
			}

            int total_length = segment_size+PARA_SIZE;
			//append necessary message after data and send them to DPU
			pack_parameters(send_buffer, total_length, task_id, send_segment_id, sendcount/segment_number, sendtype);
            if(dpu_client_send_free(send_buffer, total_length))
            {
                printf("error in dpu_client_send_free\n");
                return 1;
            }
            else {
		    inflight_mark +=1;
                //printf("SHUFFLE SEND for host %d mission %d 's segment %d, send msg(datasize:%d type:%d typesize:%d) after append\n", \
                        my_world_rank, task_id, send_segment_id, total_length, type, type_size);
            }
		}
        // 添加一个任务,指定任务id, 用户指定的接受内存,发送的数据长度(真实)
        add_one_task(task_id,recvbuf,data_size);
        return 0;
	}
    //如果客户数据大小小于等于一个segment size（一个可以发送的话)
	//do not need segment shuffle if data_size <= MAX_SEGMENT_SIZE
	//we can send all data in one dpu_client_send
	else {
		// 首先将发送的数据拷贝一次连接成带参数的数据
        int total_length = data_size+PARA_SIZE;
        char * send_buf = (char *)malloc(total_length);
        memset(send_buf,0,total_length);
        memcpy(send_buf,sendbuf,send_lenth);
        //append necessary message after data and send them to DPU
        send_segment_id = 0;
        pack_parameters(send_buf,total_length,task_id,send_segment_id,sendcount,sendtype);
        if(dpu_client_send_free(send_buf,total_length))
        {
            printf("error in dpu_client_send_free\n");
            return 1;
        }
        else {
            // 添加一个任务,指定任务id, 用户指定的接受内存,发送的数据长度(真实)
	    inflight_mark +=1;
            add_one_task(task_id,recvbuf,data_size);
            //printf("SEND for host %d mission %d 's segment %d, send msg(datasize:%d type:%d typesize:%d) after append\n", \
                my_world_rank, task_id, send_segment_id, total_length, type, type_size);
        }
	}
    return 0;
}

int my_mpi_test(uint task_id,int * flag)
{
    char * message = NULL;
    int recv_len = 0;
    if(dpu_client_try_recv(&message,&recv_len))
    {
        printf("error in dpu_client_try_recv\n");
        return 1;
    }
	//printf("recv_len %d\n", recv_len);
    // 如果收到了一个数据包的话
    if(message != NULL && recv_len >0)
    {
	inflight_mark -=1;
        uint32_t id_re;
        uint32_t in_in_re;
        unpack_parameters(message,recv_len,&id_re,&in_in_re);
		//printf("task_id %d,in_in_re %d,  recv_len %d\n", id_re, in_in_re, recv_len);
        // 寻找任务表格
        for(int i=0;i< MAX_SEND_WR ;i++)
        {
            // 找到这个task 表格
            if(my_task[i].busy == 1 && my_task[i].task_id == id_re)
            {
                // 获取用户指定的内存块
                char * recv_buf = my_task[i].recv_buf;
                // 如果是小任务的话
                if(my_task[i].need_recv_byte <= MAX_SEGMENT_SIZE - PARA_SIZE)
                {
                    // 拷贝实际到用户指定的内存
                    memcpy(recv_buf,message,recv_len - PARA_SIZE);
                    // 更新表格,更新已经接收到的实际长度
                    my_task[i].recv_byte += recv_len - PARA_SIZE;
                    // free 接受内存
                    free(message);
                }
                // 如果不是小任务的话
                else{
					int recv_place = 0;
					int segment_size = recv_len - PARA_SIZE;
					int segment_number = my_task[i].need_recv_byte / segment_size;
					int small_block_size = segment_size / my_world_size;
					int block_size = small_block_size * segment_number;
                    // 拷贝实际到用户指定的内存
					for(int i=0; i<my_world_size; i++) {
						recv_place = i*block_size + in_in_re*small_block_size;
						memcpy(recv_buf+recv_place,message+i*small_block_size,small_block_size);
					}
                    // 更新表格,更新已经接收到的实际长度
                    my_task[i].recv_byte += recv_len - PARA_SIZE;
                    // free 接受内存
                    free(message);
                }
                break;
            }
            if(i == MAX_SEND_WR -1)
            {
                printf("no task element ? error\n");
                return 1;
            }
        }
    }
    // 轮询表格查看是否收到这个指定的task id
    for(int i=0;i<MAX_SEND_WR;i++)
    {
        // 如果找到
        if(my_task[i].busy == 1 && my_task[i].task_id == task_id)
        {
		//printf("task recv data %d\n",my_task[i].recv_byte);
            // 如果足够
            if( my_task[i].recv_byte == my_task[i].need_recv_byte)
            {
                *flag = 1;
                // 如果找到了的话,那么久将这个记录删除
                my_task[i].busy = 0;
                return 0;
            }
            // 如果不足够
            else{
                *flag = 0;
                return 0;
            }
        }
    }
    printf("Error: fail to find vaild element in slots \n");
    return 1;
}


int my_mpi_clean()
{
    dpu_client_clean();
    return 0;
}
int my_mpi_alltoall(const void *sendbuf, int sendcount,
            MPI_Datatype sendtype, void *recvbuf, int recvcount,
            MPI_Datatype recvtype, uint32_t task_id,uint32_t send_lenth)
{
    if(my_mpi_ialltoall(sendbuf,sendcount,sendtype,recvbuf,recvcount,recvtype,task_id,send_lenth))
    {
	    // can not send this task
	    printf("can not send this task, try again later\n");
	    return 1;
    }
    /// 因为是阻塞式的等待结束所以循环的test直到成功
    int flag= 0;
    int querry_task = task_id;
    // 如果没有找到就一直找啊找啊找啊，找到外婆桥
    while(flag== 0)
    {
        if(my_mpi_test(querry_task,&flag))
        {
            printf("error at using my_mpi_test\n");
            return 1;
        }
    }
    return 0;
}
int my_mpi_test_for_ready(uint32_t ** task_ids,uint32_t *read_lenth)
{
    char * message = NULL;
    int recv_len = 0;
    if(dpu_client_try_recv(&message,&recv_len))
    {
        printf("error recv\n");
        return 1;
    }
    // 如果收到了一个数据包的话
    if(message != NULL && recv_len >0)
    {
	    inflight_mark -=1;
        uint32_t id_re;
        uint32_t in_in_re;
        unpack_parameters(message,recv_len,&id_re,&in_in_re);
        // 寻找任务表格
	for(int i=0;i< MAX_SEND_WR ;i++)
    {
        // ?~I??~H??~Y个task 表?| ?
        if(my_task[i].busy == 1 && my_task[i].task_id == id_re)
        {
            // ?~N??~O~V?~T??~H??~L~G?~Z?~Z~D?~F~E?~X?~]~W
            char * recv_buf = my_task[i].recv_buf;
            // ?~B?~^~\?~X??~O任?~J??~Z~D?~]
            if(my_task[i].need_recv_byte <= MAX_SEGMENT_SIZE - PARA_SIZE)
            {
                // ?~K??~]?~^?~Y~E?~H??~T??~H??~L~G?~Z?~Z~D?~F~E?~X
                memcpy(recv_buf,message,recv_len - PARA_SIZE);
                // ?~[??~V?表?| ?,?~[??~V?已?~O?~N??~T??~H??~Z~D?~^?~Y~E?~U?度
                my_task[i].recv_byte += recv_len - PARA_SIZE;
                // free ?~N??~O~W?~F~E?~X
                free(message);
            }
            // ?~B?~^~\?~M?~X??~O任?~J??~Z~D?~]
            else{
                                    int recv_place = 0;
                                    int segment_size = recv_len - PARA_SIZE;
                                    int segment_number = my_task[i].need_recv_byte / segment_size;
                                    int small_block_size = segment_size / my_world_size;
                                    int block_size = small_block_size * segment_number;
                // ?~K??~]?~^?~Y~E?~H??~T??~H??~L~G?~Z?~Z~D?~F~E?~X
                                    for(int i=0; i<my_world_size; i++) {
                                        recv_place = i*block_size + in_in_re*small_block_size;
                                            memcpy(recv_buf+recv_place,message+i*small_block_size,small_block_size);
                                    }
                // ?~[??~V?表?| ?,?~[??~V?已?~O?~N??~T??~H??~Z~D?~^?~Y~E?~U?度
                my_task[i].recv_byte += recv_len - PARA_SIZE;
                // free ?~N??~O~W?~F~E?~X
                free(message);
            }
            break;
        }
        if(i == MAX_SEND_WR -1)
        {
            printf("no task element ? error\n");
            return 1;
        }
    }

    }

    // 初始化完成数组和完成个数
    *task_ids = ( uint32_t *)malloc(sizeof(uint32_t)*MAX_SEND_WR);
    memset(*task_ids,0,sizeof(uint32_t)*MAX_SEND_WR);
    *read_lenth = 0;

    /// 遍历返回完成任务
    for(int i =0;i< MAX_SEND_WR;i++)
    {
        // 如果有效并且收齐
        if(my_task[i].busy == 1 && my_task[i].recv_byte == my_task[i].need_recv_byte)
        {
            // 重置记录表和飞行task
            my_task[i].busy = 0;
            // 添加到查询信息
            (*task_ids)[*read_lenth] = my_task[i].task_id;
            (*read_lenth) ++;
        }
    }
    return 0;

}
