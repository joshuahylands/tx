CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c17 -g -o bin/tx

all:
	mkdir -p bin
	$(CC) $(CFLAGS) src/*.c

