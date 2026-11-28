#include "gfx.h"

#include <string.h>

void n0_lienzo_init(n0_lienzo_t *l, uint8_t *bits, int16_t ancho, int16_t alto) {
    l->bits = bits;
    l->ancho = ancho;
    l->alto = alto;
    l->paso = (int16_t)((ancho + 7) / 8);
    l->hay_recorte = false;
    l->recorte = (n0_rect_t){0, 0, ancho, alto};
    n0_limpiar(l);
}

void n0_limpiar(n0_lienzo_t *l) {
    memset(l->bits, 0, (size_t)l->paso * (size_t)l->alto);
}

void n0_recorte(n0_lienzo_t *l, n0_rect_t r) {
    l->recorte = r;
    l->hay_recorte = true;
}

void n0_sin_recorte(n0_lienzo_t *l) {
    l->hay_recorte = false;
}

static bool dentro(const n0_lienzo_t *l, int16_t x, int16_t y) {
    if (x < 0 || y < 0 || x >= l->ancho || y >= l->alto) return false;
    if (l->hay_recorte) {
        if (x < l->recorte.x || y < l->recorte.y) return false;
        if (x >= l->recorte.x + l->recorte.w || y >= l->recorte.y + l->recorte.h) return false;
    }
    return true;
}

void n0_punto(n0_lienzo_t *l, int16_t x, int16_t y, uint8_t v) {
    if (!dentro(l, x, y)) return;
    uint8_t *b = &l->bits[(size_t)y * l->paso + (size_t)(x >> 3)];
    uint8_t m = (uint8_t)(0x80u >> (x & 7));
    if (v) {
        *b |= m;
    } else {
        *b &= (uint8_t)~m;
    }
}

uint8_t n0_leer(const n0_lienzo_t *l, int16_t x, int16_t y) {
    if (x < 0 || y < 0 || x >= l->ancho || y >= l->alto) return 0;
    return (uint8_t)((l->bits[(size_t)y * l->paso + (size_t)(x >> 3)] >> (7 - (x & 7))) & 1u);
}

void n0_rect_lleno(n0_lienzo_t *l, n0_rect_t r, uint8_t v) {
    for (int16_t y = r.y; y < r.y + r.h; y++)
        for (int16_t x = r.x; x < r.x + r.w; x++) n0_punto(l, x, y, v);
}

void n0_hlinea(n0_lienzo_t *l, int16_t x, int16_t y, int16_t w, uint8_t v) {
    for (int16_t i = 0; i < w; i++) n0_punto(l, (int16_t)(x + i), y, v);
}

void n0_vlinea(n0_lienzo_t *l, int16_t x, int16_t y, int16_t h, uint8_t v) {
    for (int16_t i = 0; i < h; i++) n0_punto(l, x, (int16_t)(y + i), v);
}

void n0_rect_borde(n0_lienzo_t *l, n0_rect_t r, uint8_t v) {
    n0_hlinea(l, r.x, r.y, r.w, v);
    n0_hlinea(l, r.x, (int16_t)(r.y + r.h - 1), r.w, v);
    n0_vlinea(l, r.x, r.y, r.h, v);
    n0_vlinea(l, (int16_t)(r.x + r.w - 1), r.y, r.h, v);
}

void n0_invertir(n0_lienzo_t *l, n0_rect_t r) {
    for (int16_t y = r.y; y < r.y + r.h; y++)
        for (int16_t x = r.x; x < r.x + r.w; x++)
            if (dentro(l, x, y)) n0_punto(l, x, y, (uint8_t)!n0_leer(l, x, y));
}
