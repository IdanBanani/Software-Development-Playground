CC=gcc
# CFLAGS=-Wall -Wextra -pedantic -o
CFLAGS=-Wall -pedantic -o 
SERVER_SRC=$(SRC_FOLDER)/server/main.c
CLIENT_SRC=$(SRC_FOLDER)/client/main.c
SRC_FOLDER= ./src
BIN_FOLDER=./bin
SERVER_DEPENDENCIES=$(SRC_FOLDER)/server/socket_api.c  $(SRC_FOLDER)/common/protocol.c
COMMON_INCLUDE=$(SRC_FOLDER)/common/ 
SERVER_INCLUDE=$(SRC_FOLDER)/server/ 
CLIENT_DEPENDENCIES=$(SRC_FOLDER)/client/termios2.c $(SRC_FOLDER)/common/protocol.c 
CLIENT_INCLUDE=$(SRC_FOLDER)/client
CLIENT_LDFLAGS=-pthread
SERVER_LDFLAGS=-pthread
DFLAGS = -g
SERVER_BIN=server_b
CLIENT_BIN=client_b
# BIN= $(CLIENT_BIN)
# main: main.

.PHONY: all clean
	


protocol.o:
	$(CC) -c $(DFLAGS) $(SRC_FOLDER)/common/protocol.c -o $(SRC_FOLDER)/protocol.o

termios2.o:
	$(CC) -c $(DFLAGS) $(SRC_FOLDER)/client/termios2.c -o $(SRC_FOLDER)/termios2.o

server_rule: 
	$(CC) $(DFLAGS) $(SERVER_DEPENDENCIES) $(SERVER_SRC) -I $(SERVER_INCLUDE) -I $(COMMON_INCLUDE) $(CFLAGS) $(BIN_FOLDER)/$(SERVER_BIN) $(SERVER_LDFLAGS)
client_rule: 
	$(CC) $(DFLAGS) $(CLIENT_DEPENDENCIES) $(CLIENT_SRC) -I $(CLIENT_INCLUDE) -I $(COMMON_INCLUDE) $(CFLAGS) $(BIN_FOLDER)/$(CLIENT_BIN) $(CLIENT_LDFLAGS)

all: server_rule client_rule
#touch $(BIN_FOLDER)/$(CLIENT_BIN)
#touch $(BIN_FOLDER)/$(SERVER_BIN)
# make -n server
# make -n client

default:
	make all

clean:
	rm $(BIN_FOLDER)/$(SERVER_BIN)
	rm $(BIN_FOLDER)/$(CLIENT_BIN)
