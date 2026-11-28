#include "fuente.h"

#include <stddef.h>

int n0_utf8(const char *s, uint32_t *cp) {
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] == 0) {
        *cp = 0;
        return 0;
    }
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *cp = (uint32_t)((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = (uint32_t)((u[0] & 0x0Fu) << 12) | (uint32_t)((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
        return 3;
    }
    if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        *cp = (uint32_t)((u[0] & 0x07u) << 18) | (uint32_t)((u[1] & 0x3Fu) << 12) |
              (uint32_t)((u[2] & 0x3Fu) << 6) | (u[3] & 0x3Fu);
        return 4;
    }
    // Byte inválido: se consume uno y se pinta el sustituto. No se aborta: un
    // texto mal codificado tiene que verse raro, no tumbar la pantalla.
    *cp = 0xFFFD;
    return 1;
}

// Los glifos están ordenados por punto de código, así que búsqueda binaria.
static const n0_glifo_t *buscar(const n0_fuente_t *f, uint32_t cp) {
    int lo = 0, hi = f->n_glifos - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->glifos[m].cp == cp) return &f->glifos[m];
        if (f->glifos[m].cp < cp) {
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    // Sin glifo: se cae a '?' para que se vea que falta algo en vez de que el
    // texto se acorte en silencio.
    if (cp != '?') return buscar(f, '?');
    return NULL;
}

int16_t n0_texto_ancho_n(const n0_fuente_t *f, const char *s, int n) {
    int16_t w = 0;
    int i = 0;
    while (s[i] != 0 && (n < 0 || i < n)) {
        uint32_t cp;
        int k = n0_utf8(&s[i], &cp);
        if (k == 0) break;
        const n0_glifo_t *g = buscar(f, cp);
        if (g) w = (int16_t)(w + g->avance);
        i += k;
    }
    return w;
}

int16_t n0_texto_ancho(const n0_fuente_t *f, const char *s) {
    return n0_texto_ancho_n(f, s, -1);
}

static void glifo_pintar(n0_lienzo_t *l, const n0_fuente_t *f, const n0_glifo_t *g,
                         int16_t x, int16_t y, uint8_t color) {
    if (g->w <= 0 || g->h <= 0) return;
    int paso = (g->w + 7) / 8;
    for (int gy = 0; gy < g->h; gy++) {
        for (int gx = 0; gx < g->w; gx++) {
            uint8_t by = f->bits[g->off + (size_t)gy * paso + (size_t)(gx / 8)];
            if (by & (0x80u >> (gx % 8))) {
                n0_punto(l, (int16_t)(x + g->xoff + gx), (int16_t)(y + g->yo + gy), color);
            }
        }
    }
}

int16_t n0_texto_pintar(n0_lienzo_t *l, const n0_fuente_t *f, int16_t x, int16_t y,
                        const char *s, uint8_t color) {
    int16_t x0 = x;
    int i = 0;
    while (s[i] != 0) {
        uint32_t cp;
        int k = n0_utf8(&s[i], &cp);
        if (k == 0) break;
        const n0_glifo_t *g = buscar(f, cp);
        if (g) {
            glifo_pintar(l, f, g, x, y, color);
            x = (int16_t)(x + g->avance);
        }
        i += k;
    }
    return (int16_t)(x - x0);
}

int n0_texto_pintar_ancho(n0_lienzo_t *l, const n0_fuente_t *f, int16_t x, int16_t y,
                          const char *s, int16_t ancho_max, uint8_t color) {
    if (n0_texto_ancho(f, s) <= ancho_max) {
        n0_texto_pintar(l, f, x, y, s, color);
        return 0;
    }
    // No cabe: se recorta y se marca con elipsis. Quien llama sabrá si lo que
    // procede es paginar en vez de truncar.
    const char *ell = "…";
    int16_t w_ell = n0_texto_ancho(f, ell);
    int16_t cursor = x;
    int i = 0;
    while (s[i] != 0) {
        uint32_t cp;
        int k = n0_utf8(&s[i], &cp);
        if (k == 0) break;
        const n0_glifo_t *g = buscar(f, cp);
        if (!g) {
            i += k;
            continue;
        }
        if (cursor + g->avance + w_ell > x + ancho_max) break;
        glifo_pintar(l, f, g, cursor, y, color);
        cursor = (int16_t)(cursor + g->avance);
        i += k;
    }
    n0_texto_pintar(l, f, cursor, y, ell, color);
    return 1;
}
