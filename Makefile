# -pthread e necessario so para sem_open/sem_wait em versoes antigas da glibc.
# Nenhuma pthread e criada: as threads sao do nosso escalonador (ucontext).
CC = gcc
CFLAGS = -Wall -O2
LDLIBS = -pthread

# Valores usados por "make run" (ex.: make run N=0 X=10)
N = 100
X = 10

# No WSL, o disco do Windows (/mnt/...) deixa as operacoes de arquivo muito
# lentas. Nesse caso o projeto e copiado para uma pasta nativa do Linux e
# compilado la, e aqui fica um atalho "atendimento" que roda essa versao.
# Em um Linux normal, compila aqui mesmo.
DESTINO = $(HOME)/trabalho_so

ifneq ($(filter /mnt/%,$(CURDIR)),)

all:
	rm -rf $(DESTINO)
	mkdir -p $(DESTINO)
	cp *.c Makefile $(DESTINO)
	$(MAKE) -C $(DESTINO)
	printf '#!/bin/sh\ncd $(DESTINO) && exec ./atendimento "$$@"\n' > atendimento
	chmod +x atendimento
	@echo "Pronto. Para rodar: ./atendimento N X"

run: all
	cd $(DESTINO) && ./atendimento $(N) $(X)

clean:
	rm -f atendimento analista cliente demanda.txt lng.txt analista_pid.txt
	rm -rf $(DESTINO)

else

all: atendimento analista cliente

run: all
	./atendimento $(N) $(X)

clean:
	rm -f atendimento analista cliente demanda.txt lng.txt analista_pid.txt

endif

atendimento: atendimento.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

analista: analista.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

cliente: cliente.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

.PHONY: all run clean
