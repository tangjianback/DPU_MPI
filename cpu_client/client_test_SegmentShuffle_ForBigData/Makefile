CC = mpicc
LDFLAGS = -libverbs
all:client 
client: client_main_rc.c pingpong.c client_rc.c my_mpi_op.c my_mpi_op.h pingpong.h client_rc.h
	$(CC) -g pingpong.c client_rc.c my_mpi_op.c client_main_rc.c -o  client $(LDFLAGS)

clean:
	rm client
