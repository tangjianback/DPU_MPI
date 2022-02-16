#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include "my_mpi_op.h"
#include <mpi.h>
#define TEST_ARRAY_SIZE 1000      // define the size of every task(how many integer 1000 = 4000Byte)
#define SEND_TASK 100             // define the how much task you would want to send totally
int main(int argc, char *argv[])
{
    // normal initial using MPI_XXX , actually if you do not need to use other MPI_XXX functions, you only need to call MPI_Init(NULL, NULL) for us;///
    MPI_Init(NULL, NULL); 
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank); 
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    // init the our my_mpi envirment
    my_mpi_init();

    // create data
    int my_values[TEST_ARRAY_SIZE];
    for(int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        my_values[i] = my_rank * TEST_ARRAY_SIZE + i;
    }
    // create a recv buffer
    int my_values2[TEST_ARRAY_SIZE];

    // used to record how much task you have send /recv
    int send_task_num = 0;
    int recv_task_num = 0;
    while(1)
    {
        // send task until the number of sent tasks reach SEND_TASK
        if(send_task_num < SEND_TASK)
        {
            // if error, do not worry. just try it later
            if(my_mpi_ialltoall(my_values,TEST_ARRAY_SIZE/world_size,MPI_INT,my_values2,TEST_ARRAY_SIZE/world_size,MPI_INT,send_task_num,sizeof(int)*TEST_ARRAY_SIZE))
            {
                //printf("alltoall error, but do not worry,you can try it later\n");
            }
            // if success, everything just be fine
            else{
                send_task_num ++;
            }
        }

        // prepare a querry array and length, we will get how many tasks finished(stored in get_counter)
        // and what are them(stored in id_array)
        uint32_t * id_array= NULL;
        uint32_t get_counter = 0;

        // test_once
        my_mpi_test_for_ready(&id_array,&get_counter);
        // increse recv_task_num and free id_array if you do not want to know more
        // about them
        recv_task_num += get_counter;
        free(id_array);

        // if we can enough tasks ,ok ,we are down.
        if(recv_task_num == SEND_TASK)
        {
            printf("get all data.............\n");
            break;
        }
    }
    // before the program return ,remenber my_mpi_clean.
    my_mpi_clean();
    MPI_Finalize();
    return 0;
}
