#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "server_rc.h"
#include <mpi.h>
#define PARA_SIZE 32
int my_rank;

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
    printf("budui zhuanhuan \n");
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
            printf("zhuan huan error\n");
			return NULL;
	}

}


struct record_tj{
    MPI_Request request;
    void * pointer_source;
    char busy;
    uint32_t work_id;
    uint32_t inner_work_id;
    uint32_t message_size;
    void * pointer;
};
// 任务结构体
struct record_tj result_record [MAX_SEND_WR];
void my_mpi_init()
{
    memset(result_record,0,sizeof(struct record_tj) * MAX_SEND_WR);
    return;
}

// 传入一个request到数组中，包括mission ,request, recv_buf,和整个recv_buf的长度(加上4字节的任务号码)
int put_request_in_record(uint32_t mission, MPI_Request requestout,void * recv_buf,void * source_data,uint32_t size,uint32_t inner_id)
{
    for(int i=0;i<MAX_SEND_WR;i++)
    {
        // 如果有空位
        if(result_record[i].busy == 0)
        {
            result_record[i].busy = 1;
            result_record[i].message_size = size;
            result_record[i].request = requestout;
            result_record[i].work_id = mission;
            result_record[i].inner_work_id = inner_id;
            result_record[i].pointer = recv_buf;
            result_record[i].pointer_source = source_data;
            return 0;
        }
    }
    printf("too much outstanding tasks\n");
    return 1;   
}
int pack_parameters(char * buf,uint32_t total_lenth,uint32_t id,uint32_t inner_id);
// 传给我接受的长度,返回成功的个数
int test_for_okk_mission(int *okk_len)
{
    *okk_len = 0;
    for(int i=0;i<MAX_SEND_WR;i++)
    {
        // 如果存在任务那么就test一下
        if(result_record[i].busy == 1)
        {
            int flag;
            MPI_Test(&(result_record[i].request),&flag,MPI_STATUS_IGNORE);
            // 如果已经好了的话
            if(flag==1)
            {
                //printf("Process %d,  processed = %d, %d, %d.\n", my_rank, ((int *)result_record[i].pointer)[0], ((int *)result_record[i].pointer)[1], ((int *)result_record[i].pointer)[2]);
                // 先组合一下数据
                pack_parameters(result_record[i].pointer,result_record[i].message_size,result_record[i].work_id,result_record[i].inner_work_id);
                // 然后发送数据(对结果数据使用ib free)
                dpu_server_send_free(result_record[i].pointer,result_record[i].message_size);
                // 释放mpi_ialltoall的源数据
                free(result_record[i].pointer_source);
                // 设置busy 位置为0
                memset(&result_record[i], 0,sizeof(struct record_tj));
                (*okk_len) ++;
            }
        }
    }  
    return 0;
}

// 解析数据包
int unpack_parameters(char * recv_data,uint32_t total_lenth, uint32_t *id, uint32_t *inner_id,uint32_t *each_process,MPI_Datatype * data_type)
{
    char * begin_addr = recv_data + total_lenth - PARA_SIZE;
    *id = ((uint32_t *)begin_addr)[0];
    *inner_id = ((uint32_t *)begin_addr)[1];
    *each_process = ((uint32_t *)begin_addr)[2];
    *data_type = DPU_to_MPI(((uint32_t *)begin_addr)[3]);
    return 0;
}
// 组合数据包
int pack_parameters(char * buf,uint32_t total_lenth,uint32_t id,uint32_t inner_id)
{
    char * begin_addr =  buf + total_lenth - PARA_SIZE;
    ((uint32_t *)begin_addr)[0] = id;
    ((uint32_t *)begin_addr)[1] = inner_id;
    return 0;
}



int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);
    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
   
    
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    // ?~N??~O~V?~\??~\?rank
    int fix_name_length = 100;
    int name_length;
    char* processorName = (char*)malloc(fix_name_length);
    memset(processorName, 0, fix_name_length);
    MPI_Get_processor_name(processorName, &name_length);
   
    printf("process name:%s rank:%d\n", processorName, my_rank);
    char* processNameRecvBuff = (char*)malloc(fix_name_length*size);
    memset(processNameRecvBuff, 0, fix_name_length*size);
    memcpy(&processNameRecvBuff[my_rank*fix_name_length], processorName, fix_name_length);
    // collect hostnames, form comma separated list
    MPI_Allgather(processorName, fix_name_length, MPI_CHAR, processNameRecvBuff, fix_name_length, MPI_CHAR, MPI_COMM_WORLD);
    printf("mpi all reduce finished\n");
    for(int i = 0;  i < size; i++){
        processNameRecvBuff[fix_name_length*i+fix_name_length-1] = '\0';
        printf("rank%d: process name:%s\n", i, &processNameRecvBuff[fix_name_length*i]);
    }
    int localRank = 0;
    for(int i = 0;  i < my_rank; i++){
        if(strcmp(processorName, &processNameRecvBuff[fix_name_length*i]) == 0){
                localRank += 1;
        }
    }
    printf("localRank of process:%d is %d", my_rank, localRank);
    dpu_server_init(8888+localRank);

    /////////////////////////////////////////////////////////////////////////////

    my_mpi_init();

    

    while(1){
        // 尝试获取一次segment from client
        // 收到后需要自己free
        char * recv_from_client_buf = NULL;
        int actual_getlenth = 0;
        if(dpu_server_try_recv(&recv_from_client_buf, &actual_getlenth))
        {
            printf("dpu server recv data error\n");
        }
        // 如果获取到数据
        if(actual_getlenth > 0 && recv_from_client_buf != NULL)
        {
            // 获取任务号码
            uint32_t mission_id =0;
            uint32_t inner_mission_id = 0;
            uint32_t each_process = 0;
            MPI_Datatype data_type;
            unpack_parameters(recv_from_client_buf,actual_getlenth,&mission_id,&inner_mission_id,&each_process,&data_type);
            printf("id %d; in_id %d; each_p %d; d_type %d; data_lenth %d;\n",mission_id,inner_mission_id,each_process,MPI_to_DPU(data_type),actual_getlenth - PARA_SIZE);
            
            void * recv_from_mpi = (void *)malloc(actual_getlenth);
            memset(recv_from_mpi,0,actual_getlenth);
            // 创建request
            MPI_Request request;
            // 异步调用ialltoall
            MPI_Ialltoall(recv_from_client_buf,each_process, data_type, recv_from_mpi, each_process, data_type, MPI_COMM_WORLD, &request);
            // 对ialltoall结果保存(其中source buffer和结果数据buffer都不需要在这里free)
            if(put_request_in_record(mission_id,request,recv_from_mpi,recv_from_client_buf,actual_getlenth,inner_mission_id))
            {
                printf("can not put request in record\n");
                break;
            }
        }
        // 查看是否有好了的request
        int okk_len = 0;

        if(test_for_okk_mission(&okk_len))
        {
            printf("test for okk error\n");
        }
        // if(okk_len >0)
        //     printf("get %d okk request\n",okk_len);
    }

    sleep(2);
    MPI_Finalize();
    dpu_server_clean();

    return 0;

}
