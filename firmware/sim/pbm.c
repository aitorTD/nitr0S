#include "pbm.h"

#include <stdio.h>

int n0_pbm_escribir(const n0_lienzo_t *l, const char *ruta) {
    FILE *f = fopen(ruta, "wb");
    if (!f) return -1;
    fprintf(f, "P4\n%d %d\n", l->ancho, l->alto);
    // En PBM un bit a 1 es NEGRO, igual que en el lienzo: coincide tal cual.
    size_t n = (size_t)l->paso * (size_t)l->alto;
    size_t escritos = fwrite(l->bits, 1, n, f);
    fclose(f);
    return escritos == n ? 0 : -1;
}
