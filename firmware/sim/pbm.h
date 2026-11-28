// Salida del simulador: PBM crudo, que es un formato de 1 bit por píxel y por
// tanto exactamente lo que el lienzo ya tiene. Sin conversión, sin pérdida y
// sin dependencias.
#ifndef NITR0S_SIM_PBM_H
#define NITR0S_SIM_PBM_H
#include "../nucleo/gfx.h"
int n0_pbm_escribir(const n0_lienzo_t *l, const char *ruta);
#endif
