

.DEFAULT_GOAL := default
# all
CC=gcc 
CFLAGS = -Wall -g
BIN = simulator # Binary for make all

# words
SRC = traffic_cycle.c # Sources for executable
LIBS = -pthread # pthread library (sometimes -lpthread doesn't work)


$(BIN):
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o $(BIN)

all:
	make $(BIN)

# if just "make" was called in current directory
default: all

clean:
	rm -f $(BIN)