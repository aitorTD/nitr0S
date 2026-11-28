# Simulador de escritorio. El núcleo se compila con las mismas opciones
# estrictas que se usarán en el aparato.
CC      ?= gcc
CFLAGS  ?= -std=c11 -pedantic -Wall -Wextra -Werror -O2 -g
NUCLEO  := nucleo/gfx.c nucleo/vista.c nucleo/fuente.c nucleo/render.c \
           nucleo/maquetas.c nucleo/lanzador.c \
           nucleo/fuente_cuerpo.c nucleo/fuente_titulo.c
APPS    := apps/listas.c apps/tests.c apps/estado.c
SIM     := sim/main.c sim/pbm.c sim/guion.c sim/almacen_falso.c
PRUEBAS := pruebas/test_nucleo.c

.PHONY: todo sim pruebas limpiar
todo: sim pruebas

sim: $(NUCLEO) $(APPS) $(SIM)
	$(CC) $(CFLAGS) -o nitr0sim $(NUCLEO) $(APPS) $(SIM)

pruebas: $(NUCLEO) $(APPS) $(PRUEBAS)
	$(CC) $(CFLAGS) -o nitr0test $(NUCLEO) $(APPS) $(PRUEBAS)
	./nitr0test

limpiar:
	rm -f nitr0sim nitr0test *.pbm
