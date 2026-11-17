# Programmes de la Pi Zero 2W (C)
#   make          compile liaison_esp32
#   make test     compile et lance les tests du protocole
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu99 -Wall -Wextra
PROTO   := ../shared/c/protocole

all: liaison_esp32

liaison_esp32: src/liaison_esp32.c src/uart.c $(PROTO)/protocole.c $(PROTO)/protocole.h src/uart.h
	$(CC) $(CFLAGS) -I$(PROTO) -o $@ src/liaison_esp32.c src/uart.c $(PROTO)/protocole.c

test_protocole: $(PROTO)/tests/test_protocole.c $(PROTO)/tests/main_test.c $(PROTO)/protocole.c $(PROTO)/protocole.h
	$(CC) $(CFLAGS) -I$(PROTO) -o $@ $(PROTO)/tests/test_protocole.c $(PROTO)/tests/main_test.c $(PROTO)/protocole.c

test: test_protocole
	./test_protocole

clean:
	rm -f liaison_esp32 test_protocole

.PHONY: all test clean
