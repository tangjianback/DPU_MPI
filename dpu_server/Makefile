CC = mpicc
LDFLAGS = -libverbs
all:server 

server: server_main_rc.c pingpong.c server_rc.c pingpong.h server_rc.h
	$(CC) -g pingpong.c server_rc.c server_main_rc.c -o  server $(LDFLAGS)

clean:
	rm server
