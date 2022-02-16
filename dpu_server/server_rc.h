#ifndef SERVER_H
#define SERVER_H

// 最多飞行的send wr(超过就不能post send)
#define MAX_SEND_WR 64
// 每个wr最大的字节数，超过就不让你发
#define MAX_SEGMENT_SIZE 4096
// 最大的一次性接受wr个数。和MAX_SEND_WR对应
#define MAX_RECEIVE_WR 64
// 传入port👌。阻塞式的建立连接
int dpu_server_init(unsigned int port);
// 传入空指针的地址，和实际接受长度，为你阻塞式的获取一次segment
int dpu_server_recv(char **message, uint32_t * get_lenth);
// 尝试获取一次recv，如果返回NULL && 0,那么就是失败
int dpu_server_try_recv(char **message, uint32_t * get_lenth);
// 非阻塞的发送一次数据。(切记这个message，不能再send后立即free)
int dpu_server_send(char *message, uint32_t send_size);
// 非阻塞的发送一次数据,但是你不需要对传入的数据进行free
int dpu_server_send_free(char *message, uint32_t send_size);
// 完成资源的释放
int dpu_server_clean();
// 你不需要管
void* receive_array[MAX_RECEIVE_WR];
#endif /* SERVER_H */