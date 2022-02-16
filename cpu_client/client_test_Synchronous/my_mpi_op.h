#ifndef MY_MPI_OP
#define MY_MPI_OP
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "my_mpi_op.h"
#include <string.h>
#include <unistd.h>
#include "client_rc.h"
#include <mpi.h>
#define PARA_SIZE 32
#define ITERATION 5
int my_world_rank;
int my_local_rank;
int my_world_size;
char* my_processorName;
uint32_t MPI_to_DPU(MPI_Datatype data_type);
MPI_Datatype DPU_to_MPI(uint32_t dpu_type);
int TYPE_to_SIZE(MPI_Datatype data_type);
int pack_parameters(char * buf,uint32_t total_lenth,uint32_t id,uint32_t inner_id, uint32_t each_process, MPI_Datatype data_type);
int unpack_parameters(char * recv_data,uint32_t total_lenth, uint32_t *id, uint32_t *inner_id);
int my_mpi_init();
int my_mpi_clean();
int my_mpi_test(uint task_id,int * flag);
int my_mpi_test_for_ready(uint32_t ** task_ids,uint32_t *read_lenth);
int my_mpi_alltoall(const void *sendbuf, int sendcount,
            MPI_Datatype sendtype, void *recvbuf, int recvcount,
            MPI_Datatype recvtype, uint32_t task_id,uint32_t send_lenth);
int my_mpi_ialltoall(const void *sendbuf, int sendcount,
            MPI_Datatype sendtype, void *recvbuf, int recvcount,
            MPI_Datatype recvtype, uint32_t task_id, uint32_t send_lenth);
int add_one_task(uint32_t task_id, char * recv_buf, uint32_t need_recv_byte);
int check_slot_empty();


#endif /* IBV_PINGPONG_H */
