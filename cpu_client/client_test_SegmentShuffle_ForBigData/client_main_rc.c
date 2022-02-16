#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include "my_mpi_op.h"
#include <mpi.h>
#define TEST_ARRAY_SIZE 2032      // define how many data(int) you need to send
// this client example is to show you: 
// our API(my_mpi_ialltoall) can handle the situation where the total data size is bigger than the message size
// in that case, our API(my_mpi_ialltoall) can not use one send to send all the data from host to DPU, instead,
// we do what we called segment split and segment shuffle action to split the data and shuffle them into right order, and send them using multiple client_dpu_send.
// in this example:
// we have 2032 int type data(8128 bytes) to do ialltoall
// but the max message size is defined as 4096 bytes
// so all the data is split into 2 segment, and its placement is shuffled so the result is right

// of course, you can change the size of data by change the number TEST_ARRAY_SIZE defined above
// for example ,you can define TEST_ARRAY_SIZE to 12000 or 24000  
int main(int argc, char *argv[])
{
    // normal initial using MPI_XXX , actually if you do not need to use other MPI_XXX functions, you only need to call MPI_Init(NULL, NULL) for us;///
    MPI_Init(NULL, NULL); 
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    if(world_size != 4)
    {
        printf("this example expect there are four processes\n");
        return 0;
    }
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank); 
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    // init the our my_mpi envirment
    my_mpi_init();

    /* 
    create data, in this example.
    process 0:  0  1  2  3  4  5  6  7....2031
    process 1:  2032 2033.... 4063
    process 2:  4064 ... 6095
    process 3:  6096 ... 8127

    when alltoall down,we expect that:
    using process 0 as an detailed example:
    process 0:  0 1 2 3...505 506 507,  2032 2033...2539,  4064 4065...4571,   6096 6097...6603
    */
    int my_values[TEST_ARRAY_SIZE];
    for(int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        my_values[i] = my_rank * TEST_ARRAY_SIZE + i;
    }
    ////print process 0's data before alltoall
    if(my_rank == 0) {
    printf("Process %d, before alltoall: ",my_rank);
    for(int i=0;i< TEST_ARRAY_SIZE;i++)
    {
        printf("%d, ",my_values[i]);
    }
    printf("\n");
    }

    // create a recv buffer
    int my_values2[TEST_ARRAY_SIZE];
    memset(my_values2,0 ,TEST_ARRAY_SIZE * sizeof(int));

    // alltoall until it finish the work
    if(my_mpi_ialltoall(my_values,TEST_ARRAY_SIZE/world_size,MPI_INT,my_values2,TEST_ARRAY_SIZE/world_size,MPI_INT,1,sizeof(int)*TEST_ARRAY_SIZE))
    {
        printf("some thing badly,happended\n");
    }
    int flag =0;
    while(flag == 0){
        if(my_mpi_test(1, &flag))
        {
            printf("error");
        }  
    }

    ////print process 0's data after alltoall
    if(my_rank == 0) {
    printf("Process %d, after alltoall: ",my_rank);
    for(int i=0;i< TEST_ARRAY_SIZE;i++)
    {
        printf("%d, ",my_values2[i]);
    }
    printf("\n");
    }
    // before the program return ,remenber my_mpi_clean.
    my_mpi_clean();
    MPI_Finalize();
    return 0;
}

