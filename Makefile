CC=gcc
CFLAGS=-std=gnu11 -Wall -Wextra -Werror -ggdb

bs: bs.o
test: bs.o test.o
.o: bs.h
