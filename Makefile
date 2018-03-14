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

CFLAGS+=$(shell pkg-config --cflags talloc)
LDFLAGS+=$(shell pkg-config --libs-only-other talloc)
LDFLAGS+=$(shell pkg-config --libs-only-L talloc)

ifeq ($(UNAME), FreeBSD)
LDLIBS=-lmd
endif

LDLIBS+=$(shell pkg-config --libs-only-l talloc)

.PHONY: build rebuild clean

build: dedupe

rebuild: clean build

clean:
	rm -f *.o dedupe

dedupe.o: dedupe.c
