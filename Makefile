CC=gcc
CFLAGS=-std=gnu11 -Wall -Wextra -Werror -ggdb -I./lib/sort -Wno-unused-but-set-variable -Wno-unused-function -march=native -O3
LDFLAGS=-march=native -O3

bs: bs.o
test: bs.o test.o
.o: bs.h
clean:
	rm -f bs test *.o
