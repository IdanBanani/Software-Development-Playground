CC=gcc
CFLAGS=-Wall -pedantic -o
SRC=main.c
# DEPENDENCIES=../common/common.h
DEPENDENCIES=
DFLAGS = -g
BIN=main
BIN_FOLDER = bin
# main: main.

main:
# mkdir $(BIN_FOLDER)
	$(CC) $(DFLAGS) $(SRC) $(DEPENDENCIES)  $(CFLAGS) $(PWD)/$(BIN_FOLDER)/$(BIN)

all:
	make main

default:
	make all

clean:
	rm $(PWD)/$(BIN_FOLDER)/$(BIN)
