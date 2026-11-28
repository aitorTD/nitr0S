#include "render.h"

#include <stdio.h>
#include <string.h>

int16_t n0_filas_por_pagina(const n0_maqueta_t *m, int16_t alto_lienzo) {
    int16_t util = (int16_t)(alto_lienzo - m->alto_cabecera - m->alto_barra - 2 * m->margen_y);
    if (util <= 0 || m->alto_fila <= 0) return 0;
    return (int16_t)(util / m->alto_fila);
}

static const char *marca_txt(n0_marca_t m) {
    switch (m) {
        case N0_MARCA_HECHO: return "hecho";
        case N0_MARCA_PENDIENTE: return "pendiente";
        case N0_MARCA_VENCIDO: return "vencido";
        case N0_MARCA_SIN_ENTREGAR: return "sin entregar";
        default: return "";
    }
}

// La cabecera lleva el indicador de página a la derecha. Va AQUÍ y no al pie
// del cuerpo porque allí se solapaba con la última fila: se pagina, así que
// decir en qué página estás es obligatorio, y lo obligatorio necesita sitio
// propio. Dónde exactamente es decisión visual; que no pise nada, no.
static void cabecera(n0_lienzo_t *l, const n0_maqueta_t *m, const char *titulo,
                     int16_t y0, int16_t pagina, int16_t paginas) {
    int16_t base = (int16_t)(y0 + m->f_titulo->ascenso);
    int16_t reservado = 0;
    if (paginas > 1) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d/%d", pagina + 1, paginas);
        int16_t w = n0_texto_ancho(m->f_cuerpo, buf);
        n0_texto_pintar(l, m->f_cuerpo, (int16_t)(l->ancho - m->margen_x - w), base, buf, N0_TINTA);
        reservado = (int16_t)(w + 16);
    }
    n0_texto_pintar_ancho(l, m->f_titulo, m->margen_x, base, titulo,
                          (int16_t)(l->ancho - 2 * m->margen_x - reservado), N0_TINTA);
    if (m->regla_cabecera) {
        n0_hlinea(l, m->margen_x, (int16_t)(y0 + m->alto_cabecera - 2),
                  (int16_t)(l->ancho - 2 * m->margen_x), N0_TINTA);
    }
}

static void barra_acciones(n0_lienzo_t *l, const n0_vista_t *v, const n0_maqueta_t *m,
                           int16_t barra, int16_t y0) {
    if (barra < 0) return;
    int n = 0;
    for (int16_t i = v->nodos[barra].hijo; i >= 0; i = v->nodos[i].hermano) n++;
    if (n == 0) return;

    int16_t ancho = (int16_t)((l->ancho - 2 * m->margen_x) / n);
    int16_t base = (int16_t)(y0 + (m->alto_barra + m->f_cuerpo->ascenso) / 2);
    if (m->regla_cabecera) n0_hlinea(l, m->margen_x, y0, (int16_t)(l->ancho - 2 * m->margen_x), N0_TINTA);

    int k = 0;
    for (int16_t i = v->nodos[barra].hijo; i >= 0; i = v->nodos[i].hermano, k++) {
        const char *et = v->nodos[i].a ? v->nodos[i].a : "";
        int16_t cx = (int16_t)(m->margen_x + k * ancho);
        int16_t w = n0_texto_ancho(m->f_cuerpo, et);
        // Centrado dentro de su hueco: la etiqueta se alinea con su botón, y
        // cuántos botones hay sigue siendo Q7.
        n0_texto_pintar(l, m->f_cuerpo, (int16_t)(cx + (ancho - w) / 2), base, et, N0_TINTA);
    }
}

static void lista(n0_lienzo_t *l, const n0_vista_t *v, const n0_maqueta_t *m,
                  int16_t nodo, int16_t y0, int16_t alto) {
    const n0_nodo_t *ln = &v->nodos[nodo];
    int16_t fpp = n0_filas_por_pagina(m, l->alto);
    int16_t y = y0;
    int16_t idx = 0;
    int16_t pintadas = 0;

    for (int16_t i = ln->hijo; i >= 0; i = v->nodos[i].hermano, idx++) {
        if (ln->pagina > 0 && idx < ln->pagina * fpp) continue;
        if (pintadas >= fpp || y + m->alto_fila > y0 + alto) break;
        const n0_nodo_t *f = &v->nodos[i];

        n0_rect_t fila = {m->margen_x, y, (int16_t)(l->ancho - 2 * m->margen_x), m->alto_fila};
        int sel = (idx == ln->sel);

        int16_t tx = (int16_t)(m->margen_x + m->sangria_marca);
        int16_t base = (int16_t)(y + (m->alto_fila + m->f_cuerpo->ascenso) / 2 - 2);

        // Si la selección se marca con cuña, la cuña ocupa el borde izquierdo,
        // así que la marca de estado se corre para no quedar debajo.
        int16_t mx = (int16_t)(m->margen_x + (m->sel_invertida ? 2 : N0_ANCHO_CUNA + 6));

        // La marca de estado va a la izquierda, delante del texto.
        const char *mk = marca_txt((n0_marca_t)f->marca);
        if (f->marca == N0_MARCA_HECHO) {
            n0_texto_pintar(l, m->f_cuerpo, mx, base, "x", N0_TINTA);
        } else if (f->marca != N0_MARCA_NINGUNA) {
            n0_texto_pintar(l, m->f_cuerpo, mx, base, "·", N0_TINTA);
        }

        int16_t ancho_sec = 0;
        if (f->b && f->b[0]) ancho_sec = (int16_t)(n0_texto_ancho(m->f_cuerpo, f->b) + 12);

        n0_texto_pintar_ancho(l, m->f_cuerpo, tx, base, f->a ? f->a : "",
                              (int16_t)(fila.w - m->sangria_marca - ancho_sec - 4), N0_TINTA);
        if (ancho_sec > 0) {
            n0_texto_pintar(l, m->f_cuerpo,
                            (int16_t)(fila.x + fila.w - ancho_sec + 12 - n0_texto_ancho(m->f_cuerpo, f->b)),
                            base, f->b, N0_TINTA);
        }
        (void)mk;

        if (sel) {
            if (m->sel_invertida) {
                // Una barra de selección invertida es todo el truco: es el
                // acuse de recibo más barato que admite un e-ink.
                n0_invertir(l, fila);
            } else {
                n0_rect_lleno(l, (n0_rect_t){m->margen_x, y, N0_ANCHO_CUNA, m->alto_fila}, N0_TINTA);
            }
        }
        if (m->regla_filas && !sel) {
            n0_hlinea(l, m->margen_x, (int16_t)(y + m->alto_fila - 1), fila.w, N0_TINTA);
        }
        y = (int16_t)(y + m->alto_fila);
        pintadas++;
    }

}

static void bloque_centrado(n0_lienzo_t *l, const n0_maqueta_t *m, int16_t y0, int16_t alto,
                            const char *l1, const char *l2, const char *l3) {
    const char *ls[3] = {l1, l2, l3};
    int n = 0;
    for (int i = 0; i < 3; i++)
        if (ls[i] && ls[i][0]) n++;
    int16_t h = (int16_t)(n * m->f_cuerpo->alto_linea);
    int16_t y = (int16_t)(y0 + (alto - h) / 2 + m->f_cuerpo->ascenso);
    for (int i = 0; i < 3; i++) {
        if (!ls[i] || !ls[i][0]) continue;
        int16_t w = n0_texto_ancho(m->f_cuerpo, ls[i]);
        n0_texto_pintar(l, m->f_cuerpo, (int16_t)((l->ancho - w) / 2), y, ls[i], N0_TINTA);
        y = (int16_t)(y + m->f_cuerpo->alto_linea);
    }
}

n0_vista_err_t n0_render(n0_lienzo_t *l, const n0_vista_t *v, const n0_maqueta_t *m) {
    n0_vista_err_t e = n0_vista_validar(v);
    if (e != N0_VISTA_OK) return e;

    n0_limpiar(l);
    const n0_nodo_t *p = &v->nodos[0];

    int16_t y_cab = m->barra_arriba ? (int16_t)(m->margen_y + m->alto_barra) : m->margen_y;
    int16_t y_barra = m->barra_arriba ? m->margen_y : (int16_t)(l->alto - m->margen_y - m->alto_barra);
    int16_t y_cuerpo = (int16_t)(y_cab + m->alto_cabecera);
    // La barra ocupa UNA franja, arriba o abajo, no las dos. Restarla siempre
    // del alto del cuerpo le robaba una fila a la maqueta de barra arriba.
    int16_t fin_cuerpo = m->barra_arriba ? (int16_t)(l->alto - m->margen_y) : y_barra;
    int16_t alto_cuerpo = (int16_t)(fin_cuerpo - y_cuerpo);

    int16_t pagina = 0, paginas = 0;
    for (int16_t i = p->hijo; i >= 0; i = v->nodos[i].hermano) {
        if (v->nodos[i].tipo == N0_NODO_LISTA || v->nodos[i].tipo == N0_NODO_TEXTO) {
            pagina = v->nodos[i].pagina;
            paginas = v->nodos[i].paginas;
            break;
        }
    }
    cabecera(l, m, p->a ? p->a : "", y_cab, pagina, paginas);

    int16_t barra = -1;
    for (int16_t i = p->hijo; i >= 0; i = v->nodos[i].hermano) {
        const n0_nodo_t *n = &v->nodos[i];
        switch (n->tipo) {
            case N0_NODO_LISTA:
                lista(l, v, m, i, y_cuerpo, alto_cuerpo);
                break;
            case N0_NODO_PREGUNTA: {
                // Enunciado arriba, opciones debajo como filas seleccionables.
                // La opción marcada se acusa invirtiendo su fila, que es el
                // acuse más barato que admite un e-ink.
                int16_t y = (int16_t)(y_cuerpo + m->f_cuerpo->ascenso + 4);
                n0_texto_pintar_ancho(l, m->f_cuerpo, m->margen_x, y, n->a ? n->a : "",
                                      (int16_t)(l->ancho - 2 * m->margen_x), N0_TINTA);
                y = (int16_t)(y + m->f_cuerpo->alto_linea + 10);
                int16_t k = 0;
                for (int16_t o = n->hijo; o >= 0; o = v->nodos[o].hermano, k++) {
                    n0_rect_t fila = {m->margen_x, (int16_t)(y - m->f_cuerpo->ascenso),
                                      (int16_t)(l->ancho - 2 * m->margen_x), m->alto_fila};
                    char letra[8];
                    snprintf(letra, sizeof(letra), "%c)", (char)('a' + k));
                    n0_texto_pintar(l, m->f_cuerpo, (int16_t)(m->margen_x + 6), y, letra, N0_TINTA);
                    n0_texto_pintar_ancho(l, m->f_cuerpo, (int16_t)(m->margen_x + m->sangria_marca),
                                          y, v->nodos[o].a ? v->nodos[o].a : "",
                                          (int16_t)(fila.w - m->sangria_marca - 6), N0_TINTA);
                    if (k == n->sel) n0_invertir(l, fila);
                    y = (int16_t)(y + m->alto_fila);
                }
                break;
            }
            case N0_NODO_VACIO:
                bloque_centrado(l, m, y_cuerpo, alto_cuerpo, n->a, "", n->c);
                break;
            case N0_NODO_ERROR:
                bloque_centrado(l, m, y_cuerpo, alto_cuerpo, n->b, n->c, n->a);
                break;
            case N0_NODO_OCUPADO: {
                char buf[48];
                if (n->paginas > 0) {
                    snprintf(buf, sizeof(buf), "%d de %d", n->pagina, n->paginas);
                    bloque_centrado(l, m, y_cuerpo, alto_cuerpo, n->a, buf, "");
                } else {
                    bloque_centrado(l, m, y_cuerpo, alto_cuerpo, n->a, "", "");
                }
                break;
            }
            case N0_NODO_BARRA:
                barra = i;
                break;
            default:
                break;
        }
    }
    barra_acciones(l, v, m, barra, y_barra);
    return N0_VISTA_OK;
}
