CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -Werror -std=c11
THREAD_FLAGS := -pthread
UTIL := util/networking.c

.PHONY: all server client clean

all: server client

server: socket-server/main.c $(UTIL) util/networking.h
	$(CC) $(CFLAGS) $(THREAD_FLAGS) socket-server/main.c $(UTIL) -o socket-server/server

client: socket-client/main.c $(UTIL) util/networking.h
	$(CC) $(CFLAGS) $(THREAD_FLAGS) socket-client/main.c $(UTIL) -o socket-client/client

clean:
	rm -f socket-server/server socket-client/client
