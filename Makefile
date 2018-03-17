UNAME:=$(shell uname -s)

CFLAGS=-Wall

ifdef DEBUG
CFLAGS+=-O0 -g -D_DEBUG
else
CFLAGS+=-O3
endif

ifeq ($(UNAME), Linux)
CFLAGS+=-D_GNU_SOURCE
endif

ifeq ($(UNAME), FreeBSD)
LDLIBS=-lmd
else
LDLIBS+=-lcrypto
endif

LDLIBS+=-ltalloc

.PHONY: build rebuild clean

build: dedupe

rebuild: clean build

clean:
	rm -f *.o dedupe

dedupe.o: dedupe.c
