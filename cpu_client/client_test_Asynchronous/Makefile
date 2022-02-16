CC = mpicc
LDFLAGS = -libverbs
all:client server
client: client.c pingpong.c pingpong.h
	$(CC) -g pingpong.c client.c -o  client $(LDFLAGS)
server: server.c pingpong.c pingpong.h
	$(CC) -g pingpong.c server.c -o  server $(LDFLAGS)

clean:
	rm client server
