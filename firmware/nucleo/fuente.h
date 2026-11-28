// fuente — tipografía de mapa de bits, 1 bit por píxel.
//
// §12.1 del brief: el texto se pinta con fuentes pre-renderizadas en un
// conjunto fijo de tamaños incrustadas en flash. Es una decisión de
// rendimiento, e implica que la escala tipográfica es un conjunto pequeño y
// cerrado, no una escala libre.
//
// La FAMILIA y los TAMAÑOS concretos son decisiones del autor (§12.2). Aquí
// solo está la interfaz: cambiar de tipografía es regenerar los datos con
// herramientas/genfuente, sin tocar una línea del renderizador.
#ifndef NITR0S_FUENTE_H
#define NITR0S_FUENTE_H

#include <stdint.h>

#include "gfx.h"

typedef struct {
    uint16_t cp;      // punto de código Unicode
    int8_t w, h;      // tamaño del mapa
    int8_t xoff, yo;  // desplazamiento respecto al origen de la línea base
    int8_t avance;    // cuánto avanza el cursor
    uint16_t off;     // desplazamiento dentro del vector de bits
} n0_glifo_t;

typedef struct {
    const char *nombre;
    int16_t alto_linea;
    int16_t ascenso;
    int16_t n_glifos;
    const n0_glifo_t *glifos;
    const uint8_t *bits;
} n0_fuente_t;

// Decodifica un carácter UTF-8. Devuelve cuántos bytes consumió, 0 al final.
// El texto del proyecto es UTF-8 porque el español lo necesita.
int n0_utf8(const char *s, uint32_t *cp);

// Ancho en píxeles de una cadena, y de un prefijo de n bytes.
int16_t n0_texto_ancho(const n0_fuente_t *f, const char *s);
int16_t n0_texto_ancho_n(const n0_fuente_t *f, const char *s, int n);

// Pinta en (x, línea base y). Devuelve el ancho pintado.
int16_t n0_texto_pintar(n0_lienzo_t *l, const n0_fuente_t *f, int16_t x, int16_t y,
                        const char *s, uint8_t color);

// Pinta recortando a un ancho máximo, con elipsis si no cabe. Devuelve true si
// hubo que recortar: quien llama puede decidir paginar en vez de truncar.
int n0_texto_pintar_ancho(n0_lienzo_t *l, const n0_fuente_t *f, int16_t x, int16_t y,
                          const char *s, int16_t ancho_max, uint8_t color);

#endif
