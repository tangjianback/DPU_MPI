#ifndef CLIENT_H
#define CLIENT_H

#define MAX_SEND_WR 64
#define MAX_SEGMENT_SIZE 4096
#define MAX_RECEIVE_WR 64
int dpu_client_init(char * ip,unsigned int port);
int dpu_client_recv(char **message, uint32_t * get_lenth);
int dpu_client_send(char *message, uint32_t send_size);
int dpu_client_send_free(char *message, uint32_t send_size);
int dpu_client_try_recv(char ** message, uint32_t * get_lenth);
int dpu_client_clean();
void* receive_array[MAX_RECEIVE_WR];
#endif /* CLIENT_H */