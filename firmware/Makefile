# Simulador de escritorio. El núcleo se compila con las mismas opciones
# estrictas que se usarán en el aparato.
CC      ?= gcc
CFLAGS  ?= -std=c11 -pedantic -Wall -Wextra -Werror -O2 -g
NUCLEO  := nucleo/gfx.c nucleo/vista.c nucleo/fuente.c nucleo/render.c \
           nucleo/maquetas.c nucleo/lanzador.c nucleo/opfmt.c nucleo/fusion.c \
           nucleo/almacen.c nucleo/sha256.c nucleo/sinc.c \
           nucleo/fuente_cuerpo.c nucleo/fuente_titulo.c
APPS    := apps/listas.c apps/tests.c apps/estado.c
SIM     := sim/main.c sim/pbm.c sim/guion.c sim/almacen_falso.c \
           sim/transporte.c sim/sincronizar.c
PRUEBAS := pruebas/test_nucleo.c

.PHONY: todo sim pruebas cruzado e2e limpiar
todo: sim pruebas cruzado

sim: $(NUCLEO) $(APPS) $(SIM)
	$(CC) $(CFLAGS) -o nitr0sim $(NUCLEO) $(APPS) $(SIM)

pruebas: $(NUCLEO) $(APPS) $(PRUEBAS)
	$(CC) $(CFLAGS) -o nitr0test $(NUCLEO) $(APPS) $(PRUEBAS)
	./nitr0test

# Fusiona el mismo registro que el servidor y compara el estado. El material lo
# genera el servidor con:
#   NITR0S_FIXTURE=$(PWD)/pruebas/datos go test ../servidor/internal/merge/ -run Fixture
cruzado: $(NUCLEO) pruebas/test_cruzado.c
	$(CC) $(CFLAGS) -o nitr0cruzado nucleo/opfmt.c nucleo/fusion.c pruebas/test_cruzado.c
	./nitr0cruzado pruebas/datos

# Levanta un servidor de verdad y sincroniza contra él. Necesita Go.
e2e: sim
	./pruebas/e2e.sh

limpiar:
	rm -f nitr0sim nitr0test nitr0cruzado *.pbm
