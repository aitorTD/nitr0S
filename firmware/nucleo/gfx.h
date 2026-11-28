// gfx — framebuffer monocromo de 1 bit por píxel.
//
// Es el mismo código que acabará en el aparato: el simulador no lo sustituye,
// solo le pone otra salida. Eso es lo que hace que una propuesta visual
// revisada aquí se parezca a lo que se verá en el panel (brief §12.3).
#ifndef NITR0S_GFX_H
#define NITR0S_GFX_H

#include <stdbool.h>
#include <stdint.h>

// 1 = tinta (negro), 0 = papel (blanco). El panel es reflectante: el blanco es
// el que da contraste, así que el papel es el estado de reposo.
#define N0_PAPEL 0
#define N0_TINTA 1

typedef struct {
    int16_t x, y, w, h;
} n0_rect_t;

typedef struct {
    uint8_t *bits;   // una fila cada (ancho+7)/8 bytes, bit más significativo a la izquierda
    int16_t ancho;
    int16_t alto;
    int16_t paso;    // bytes por fila
    n0_rect_t recorte;  // todo dibujo se recorta aquí; va en el lienzo y no en
    bool hay_recorte;   // un global, porque un global no es reentrante
} n0_lienzo_t;

void n0_lienzo_init(n0_lienzo_t *l, uint8_t *bits, int16_t ancho, int16_t alto);
void n0_limpiar(n0_lienzo_t *l);
void n0_punto(n0_lienzo_t *l, int16_t x, int16_t y, uint8_t v);
uint8_t n0_leer(const n0_lienzo_t *l, int16_t x, int16_t y);
void n0_rect_lleno(n0_lienzo_t *l, n0_rect_t r, uint8_t v);
void n0_rect_borde(n0_lienzo_t *l, n0_rect_t r, uint8_t v);
void n0_hlinea(n0_lienzo_t *l, int16_t x, int16_t y, int16_t w, uint8_t v);
void n0_vlinea(n0_lienzo_t *l, int16_t x, int16_t y, int16_t h, uint8_t v);

// Invertir es el acuse de recibo más barato que existe en e-ink: una barra de
// selección invertida es todo el truco (brief, técnicas de respuesta).
void n0_invertir(n0_lienzo_t *l, n0_rect_t r);

// Recorte: sirve para que una fila no pueda pintar fuera de su sitio.
void n0_recorte(n0_lienzo_t *l, n0_rect_t r);
void n0_sin_recorte(n0_lienzo_t *l);

#endif
