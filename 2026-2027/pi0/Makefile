# Programmes de la Pi Zero 2W (C)
#   make          compile liaison_esp32 et tableau_bord
#   make test     compile et lance les tests du protocole
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu99 -Wall -Wextra
PROTO   := ../shared/c/protocole
# Durcissement : debordements detectes a l'execution, binaire PIE, pile protegee
DURCI   := -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -pie -Wl,-z,relro,-z,now

all: liaison_esp32 tableau_bord

liaison_esp32: src/liaison_esp32.c src/uart.c $(PROTO)/protocole.c $(PROTO)/protocole.h src/uart.h
	$(CC) $(CFLAGS) -I$(PROTO) -o $@ src/liaison_esp32.c src/uart.c $(PROTO)/protocole.c

tableau_bord: src/tableau_bord.c src/uart.c $(PROTO)/protocole.c $(PROTO)/protocole.h src/uart.h
	$(CC) $(CFLAGS) $(DURCI) -I$(PROTO) -o $@ src/tableau_bord.c src/uart.c $(PROTO)/protocole.c

test_protocole: $(PROTO)/tests/test_protocole.c $(PROTO)/tests/main_test.c $(PROTO)/protocole.c $(PROTO)/protocole.h
	$(CC) $(CFLAGS) -I$(PROTO) -o $@ $(PROTO)/tests/test_protocole.c $(PROTO)/tests/main_test.c $(PROTO)/protocole.c

test: test_protocole
	./test_protocole

clean:
	rm -f liaison_esp32 tableau_bord test_protocole

.PHONY: all test clean
