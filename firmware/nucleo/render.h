// render — pinta un árbol de vista en un lienzo.
//
// La maqueta va APARTE, como datos. Una propuesta visual es entonces una tabla
// de constantes, no una reescritura del renderizador, y se pueden comparar dos
// o tres alternativas de la misma pantalla lado a lado con el mismo código que
// acabará en el aparato (brief §12.3).
//
// NINGUNA de las maquetas de maquetas.c está aprobada. Son material para la
// fase visual, que es del autor (§12.2).
#ifndef NITR0S_RENDER_H
#define NITR0S_RENDER_H

#include "fuente.h"
#include "gfx.h"
#include "vista.h"

// Ancho de la cuña de selección, cuando la maqueta no invierte la fila.
#define N0_ANCHO_CUNA 5

typedef struct {
    const char *nombre;
    const char *descripcion;

    int16_t margen_x;       // margen lateral
    int16_t margen_y;       // margen superior e inferior
    int16_t alto_cabecera;  // alto reservado al título
    int16_t alto_fila;      // alto de una fila de lista
    int16_t alto_barra;     // alto de la barra de acciones
    int16_t sangria_marca;  // hueco a la izquierda para la marca de estado

    int regla_cabecera;  // línea bajo la cabecera
    int regla_filas;     // línea entre filas
    int sel_invertida;   // 1 = barra invertida; 0 = cuña a la izquierda
    int barra_arriba;    // 1 = barra de acciones arriba; 0 = abajo

    const n0_fuente_t *f_titulo;
    const n0_fuente_t *f_cuerpo;
} n0_maqueta_t;

// Cuántas filas caben con esta maqueta. Es lo que necesita una app para
// paginar, porque aquí NO se hace scroll: se pagina.
int16_t n0_filas_por_pagina(const n0_maqueta_t *m, int16_t alto_lienzo);

// Pinta. Devuelve 0 si todo fue bien, o el error de validación del árbol: el
// núcleo no pinta un árbol inválido, lo rechaza.
n0_vista_err_t n0_render(n0_lienzo_t *l, const n0_vista_t *v, const n0_maqueta_t *m);

#endif
