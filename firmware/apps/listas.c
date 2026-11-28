// App Listas: compra y recordatorios unidos, con varias listas con nombre
// (decisión 8 del brief).
//
// Capacidades que necesita: store y sync_status. Nada más. Que la app más
// usada necesite dos es la señal de que la lista cerrada está bien
// dimensionada.
#include <stdio.h>
#include <string.h>

#include "../nucleo/app.h"
#include "../nucleo/render.h"

#define DS_ITEMS 1001u
#define MAX_VISIBLES 24
#define MAX_TEXTO 48

typedef struct {
    int16_t sel;
    int16_t pagina;
    int16_t fpp;  // filas por página, se lo dice el núcleo
    int16_t n;
    n0_id_t ids[MAX_VISIBLES];
    // Copia propia: el almacén reutiliza su buffer en cada llamada.
    char textos[MAX_VISIBLES][MAX_TEXTO];
    uint8_t marcas[MAX_VISIBLES];
} estado_t;

static estado_t *est(n0_ctx_t *c) { return (estado_t *)c->estado; }

static n0_err_t releer(n0_ctx_t *c) {
    estado_t *e = est(c);
    void *it = NULL;
    n0_err_t r = c->sv->lista_abrir(c, DS_ITEMS, &it);
    if (r != N0_OK) return r;
    e->n = 0;
    while (e->n < MAX_VISIBLES) {
        const char *t = NULL;
        n0_id_t id = {0, 0};
        uint8_t m = 0;
        if (c->sv->lista_siguiente(c, it, &t, &id, &m) != N0_OK) break;
        e->ids[e->n] = id;
        snprintf(e->textos[e->n], MAX_TEXTO, "%s", t ? t : "");
        e->marcas[e->n] = m;
        e->n++;
    }
    c->sv->lista_cerrar(c, it);
    if (e->sel >= e->n) e->sel = (int16_t)(e->n > 0 ? e->n - 1 : 0);
    return N0_OK;
}

static n0_err_t arrancar(n0_ctx_t *c, bool frio) {
    (void)frio;
    c->estado = n0_reservar(c, sizeof(estado_t));
    if (!c->estado) return N0_ERR_CUOTA;
    estado_t *e = est(c);
    e->sel = 0;
    e->pagina = 0;
    e->fpp = 8;  // el shell se lo corrige antes de pintar
    return releer(c);
}

static n0_err_t evento(n0_ctx_t *c, const n0_evento_t *ev) {
    estado_t *e = est(c);
    if (ev->tipo == N0_EV_DATOS_CAMBIADOS) return releer(c);
    if (ev->tipo != N0_EV_ACCION) return N0_OK;
    if (e->n == 0) return N0_OK;

    // repeat > 1: cinco pulsaciones rápidas son un salto de cinco filas y UN
    // repintado. Nunca se descartan.
    int16_t paso = (int16_t)(ev->repeat > 0 ? ev->repeat : 1);
    switch (ev->accion) {
        case N0_ACT_SIGUIENTE:
            e->sel = (int16_t)(e->sel + paso);
            if (e->sel >= e->n) e->sel = (int16_t)(e->n - 1);
            break;
        case N0_ACT_ANTERIOR:
            e->sel = (int16_t)(e->sel - paso);
            if (e->sel < 0) e->sel = 0;
            break;
        case N0_ACT_ELEGIR: {
            bool hecho = e->marcas[e->sel] == N0_MARCA_HECHO;
            n0_err_t r = c->sv->marcar(c, DS_ITEMS, e->ids[e->sel], !hecho);
            if (r != N0_OK) return r;
            return releer(c);
        }
        default:
            break;
    }
    if (e->fpp > 0) e->pagina = (int16_t)(e->sel / e->fpp);
    return N0_OK;
}

static n0_err_t pintar(n0_ctx_t *c, n0_vista_t *v) {
    estado_t *e = est(c);
    int16_t p = n0_pantalla(v, "Compra");

    if (e->n == 0) {
        // Estado vacío DISEÑADO: dice qué falta y qué hacer. Sin el "qué
        // hacer" el núcleo rechaza el árbol, así que no es opcional.
        n0_vacio(v, p, "La lista está vacía.",
                 "Añade cosas desde la consola de la Pi.");
        int16_t b0 = n0_barra(v, p);
        n0_accion(v, b0, N0_ACT_ATRAS, "Atrás");
        return N0_OK;
    }

    int16_t pags = (int16_t)(e->fpp > 0 ? (e->n + e->fpp - 1) / e->fpp : 1);
    int16_t l = n0_lista(v, p, e->sel, e->pagina, pags);
    for (int16_t i = 0; i < e->n; i++)
        n0_fila(v, l, e->textos[i], NULL, (n0_marca_t)e->marcas[i]);

    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ANTERIOR, "Arriba");
    n0_accion(v, b, N0_ACT_SIGUIENTE, "Abajo");
    n0_accion(v, b, N0_ACT_ELEGIR, e->marcas[e->sel] == N0_MARCA_HECHO ? "Desmarcar" : "Marcar");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
    return N0_OK;
}

static n0_err_t parar(n0_ctx_t *c) {
    (void)c;
    return N0_OK;
}

const n0_app_t n0_app_listas = {
    .api_required = 1,
    .id = "listas",
    .nombre = "Listas",
    .capacidades = N0_CAP_STORE | N0_CAP_SYNC_STATUS,
    .al_arrancar = arrancar,
    .al_evento = evento,
    .al_volver = NULL,
    .al_cambiar_datos = NULL,
    .al_parar = parar,
    .pintar = pintar,
};

// El shell le dice cuántas filas caben, porque eso depende de la maqueta y la
// maqueta no es cosa de la app.
void n0_listas_fijar_fpp(n0_ctx_t *c, int16_t fpp) {
    if (c->estado) est(c)->fpp = fpp;
}
