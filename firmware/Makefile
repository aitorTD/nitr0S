# Simulador de escritorio. El núcleo se compila con las mismas opciones
# estrictas que se usarán en el aparato.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2 -g
NUCLEO  := nucleo/gfx.c nucleo/vista.c nucleo/fuente.c nucleo/render.c \
           nucleo/maquetas.c nucleo/fuente_cuerpo.c nucleo/fuente_titulo.c
SIM     := sim/main.c sim/pbm.c
PRUEBAS := pruebas/test_nucleo.c

.PHONY: todo sim pruebas limpiar
todo: sim pruebas

sim: $(NUCLEO) $(SIM)
	$(CC) $(CFLAGS) -o nitr0sim $(NUCLEO) $(SIM)

pruebas: $(NUCLEO) $(PRUEBAS)
	$(CC) $(CFLAGS) -o nitr0test $(NUCLEO) $(PRUEBAS)
	./nitr0test

limpiar:
	rm -f nitr0sim nitr0test *.pbm
