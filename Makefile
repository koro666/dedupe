ifdef DEBUG
CFLAGS=-O0 -g -D_DEBUG
else
CFLAGS=-O3
endif

ifeq ($(shell uname), FreeBSD)
LDLIBS=-lmd
endif

.PHONY: build rebuild clean

build: dedupe

rebuild: clean build

clean:
	rm -f *.o dedupe

dedupe.c: dedupe.h

dedupe.o: dedupe.c
