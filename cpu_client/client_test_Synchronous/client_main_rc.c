#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include "my_mpi_op.h"
#include <mpi.h>
#define TEST_ARRAY_SIZE 8      // define how many data(int) you need to send
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
    process 1:  0 1 2  3  4  5  6  7
    process 2:  8 9 10 11 12 13 14 15
    process 3:  16 17 18 19 20 21 22 23
    process 4:  24 25 26 27 28 29 30 31

    when alltoall down,we expect that:
    process 1:  0 1 8 9 16 17 24 25
    process 2:  2 3 10 11 18 19 26 27
    process 3:  4 5 12 13 20 21 28 29
    process 4:  6 7 14 15 22 23 30 31
    */
    int my_values[TEST_ARRAY_SIZE];
    for(int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        my_values[i] = my_rank * TEST_ARRAY_SIZE + i;
    }
    printf("Process %d, before: ",my_rank);
    for(int i=0;i< TEST_ARRAY_SIZE;i++)
    {
        printf("%d, ",my_values[i]);
    }
    printf("\n");

    // create a recv buffer
    int my_values2[TEST_ARRAY_SIZE];
    memset(my_values2,0 ,TEST_ARRAY_SIZE * sizeof(int));

    // alltoall until it finish the work
    if(my_mpi_alltoall(my_values,TEST_ARRAY_SIZE/world_size,MPI_INT,my_values2,TEST_ARRAY_SIZE/world_size,MPI_INT,1,sizeof(int)*TEST_ARRAY_SIZE))
    {
        printf("some thing badly,happended\n");
    }
    else{
      
        printf("Process %d, after: ",my_rank);
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
