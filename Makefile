CC=gcc
CFLAGS=-std=gnu11 -Wall -Wextra -Werror -ggdb -I./lib/sort -Wno-unused-but-set-variable -Wno-unused-function -O3 -march=native
LDFLAGS=-O3 -march=native -lJudy

bs: bs.o
test: bs.o test.o
.o: bs.h
clean:
	rm -f bs test *.o
