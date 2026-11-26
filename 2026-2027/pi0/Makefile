# Programmes de la Pi Zero 2W (C)
#   make          compile liaison_esp32 et tableau_bord
#   make test     compile et lance les tests (protocole, puis modules de la Pi)
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu99 -Wall -Wextra
PROTO   := ../shared/c/protocole
# Durcissement : debordements detectes a l'execution, binaire PIE, pile protegee
DURCI   := -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -pie -Wl,-z,relro,-z,now

# Modules sans materiel (testes par test_pi) et module SIM7600 (USB)
MODULES := src/env.c src/sim_analyse.c src/kalman_vitesse.c src/trace.c src/sms_commandes.c src/sim_moteur.c \
	src/carte_zones.c
ENTETES := $(MODULES:.c=.h) src/sim7600.h src/uart.h $(PROTO)/protocole.h

all: liaison_esp32 tableau_bord

# Outil a lancer avec Internet pour ajouter une zone de carte (voir carte/zones.conf)
telecharger_carte: src/telecharger_carte.c src/carte_zones.c src/carte_zones.h
	$(CC) $(CFLAGS) $(DURCI) -Isrc -o $@ src/telecharger_carte.c src/carte_zones.c -lm

liaison_esp32: src/liaison_esp32.c src/uart.c $(PROTO)/protocole.c $(PROTO)/protocole.h src/uart.h
	$(CC) $(CFLAGS) -I$(PROTO) -o $@ src/liaison_esp32.c src/uart.c $(PROTO)/protocole.c

tableau_bord: src/tableau_bord.c src/uart.c src/sim7600.c $(MODULES) $(PROTO)/protocole.c $(ENTETES)
	$(CC) $(CFLAGS) $(DURCI) -I$(PROTO) -Isrc -o $@ src/tableau_bord.c src/uart.c src/sim7600.c $(MODULES) \
		$(PROTO)/protocole.c -lm

test_protocole: $(PROTO)/tests/test_protocole.c $(PROTO)/tests/main_test.c $(PROTO)/protocole.c $(PROTO)/protocole.h
	$(CC) $(CFLAGS) -I$(PROTO) -o $@ $(PROTO)/tests/test_protocole.c $(PROTO)/tests/main_test.c $(PROTO)/protocole.c

test_pi: tests/test_pi.c $(MODULES) $(MODULES:.c=.h)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_pi.c $(MODULES) -lm

test: test_protocole test_pi
	./test_protocole
	./test_pi

# Robustesse : des millions d'entrees aleatoires, avec detection des
# debordements memoire (AddressSanitizer) et des calculs indefinis (UBSan)
# -static-libasan : Raspberry Pi OS precharge libarmmem (/etc/ld.so.preload),
# ce qui empeche ASan de demarrer en bibliotheque partagee.
# -latomic : ASan en a besoin sur la Pi 32 bits.
SANITIZE := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -static-libasan
test_robustesse: tests/test_robustesse.c $(MODULES) $(MODULES:.c=.h) $(PROTO)/protocole.c $(PROTO)/protocole.h
	$(CC) -std=gnu99 -Wall -Wextra $(SANITIZE) -Isrc -I$(PROTO) -o $@ tests/test_robustesse.c $(MODULES) \
		$(PROTO)/protocole.c -lm -latomic

robustesse: test_robustesse
	./test_robustesse

# Performance : capacite de chaque traitement face au besoin reel
perf_pi: tests/perf_pi.c $(MODULES) $(MODULES:.c=.h) $(PROTO)/protocole.c $(PROTO)/protocole.h
	$(CC) $(CFLAGS) -Isrc -I$(PROTO) -o $@ tests/perf_pi.c $(MODULES) $(PROTO)/protocole.c -lm

perf: perf_pi
	./perf_pi

# Test de charge du serveur en marche (voir src/stress_tableau.c)
stress_tableau: src/stress_tableau.c
	$(CC) $(CFLAGS) -o $@ src/stress_tableau.c

clean:
	rm -f liaison_esp32 tableau_bord test_protocole test_pi telecharger_carte test_robustesse perf_pi stress_tableau

.PHONY: all test robustesse perf clean
