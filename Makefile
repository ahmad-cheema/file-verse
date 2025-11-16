CC = g++
CFLAGS = -std=c++17 -Isource/include -O2
SRCS = source/omni_core.cpp tools/fs_test.cpp
OUT = tools/fs_test

SERVER_SRCS = tools/fifo_server.cpp source/server/fifo_server.cpp source/omni_core.cpp
SERVER_OUT = tools/fifo_server

CLIENT_SRCS = tools/fs_client.cpp
CLIENT_OUT = tools/fs_client

all: $(OUT) $(SERVER_OUT) $(CLIENT_OUT)

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $(OUT) $(SRCS)

$(SERVER_OUT): $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o $(SERVER_OUT) $(SERVER_SRCS) -pthread

$(CLIENT_OUT): $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $(CLIENT_OUT) $(CLIENT_SRCS)

clean:
	rm -f $(OUT) $(SERVER_OUT) $(CLIENT_OUT) test_student.omni
