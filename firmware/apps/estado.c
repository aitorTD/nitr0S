// App Estado y diagnóstico: lo que queda de Ajustes.
//
// Es la prueba de que las apps son pantallas igual que lo es el sistema: en
// vez de un caso especial tallado en C, es una app con cinco capacidades.
#include <stdio.h>

#include "../nucleo/app.h"
#include "../nucleo/render.h"

typedef struct {
    char bateria[32];
    char sync[48];
    char reloj[40];
    char fallo[64];
    int16_t sel;
} estado_t;

static const char *sync_txt(n0_sync_estado_t s) {
    switch (s) {
        case N0_SYNC_NUNCA: return "nunca se ha sincronizado";
        case N0_SYNC_AL_DIA: return "al día";
        case N0_SYNC_PENDIENTE: return "hay cambios sin subir";
        case N0_SYNC_FALLANDO: return "fallando; reintentos espaciados";
    }
    return "?";
}

static n0_err_t arrancar(n0_ctx_t *c, bool frio) {
    (void)frio;
    c->estado = n0_reservar(c, sizeof(estado_t));
    if (!c->estado) return N0_ERR_CUOTA;
    estado_t *e = (estado_t *)c->estado;

    int pct = 0, mv = 0;
    if (c->sv->bateria && c->sv->bateria(c, &pct, &mv) == N0_OK) {
        snprintf(e->bateria, sizeof(e->bateria), "%d%% (%d mV)", pct, mv);
    } else {
        snprintf(e->bateria, sizeof(e->bateria), "sin lectura");
    }

    n0_sync_estado_t s = N0_SYNC_NUNCA;
    if (c->sv->sync_estado) c->sv->sync_estado(c, &s);
    snprintf(e->sync, sizeof(e->sync), "%s", sync_txt(s));

    n0_reloj_estado_t r = N0_RELOJ_FIABLE;
    if (c->sv->reloj_estado) c->sv->reloj_estado(c, &r);
    snprintf(e->reloj, sizeof(e->reloj), "%s",
             r == N0_RELOJ_FIABLE ? "en hora" : "dudoso desde el último apagón");

    const char *uf = NULL;
    uint32_t rein = 0;
    if (c->sv->diag && c->sv->diag(c, &uf, &rein) == N0_OK) {
        snprintf(e->fallo, sizeof(e->fallo), "%s (%u reinicios)", uf ? uf : "ninguno", rein);
    } else {
        snprintf(e->fallo, sizeof(e->fallo), "sin registro");
    }
    return N0_OK;
}

static n0_err_t evento(n0_ctx_t *c, const n0_evento_t *ev) {
    estado_t *e = (estado_t *)c->estado;
    if (ev->tipo != N0_EV_ACCION) return N0_OK;
    if (ev->accion == N0_ACT_ELEGIR && c->sv->sync_forzar) return c->sv->sync_forzar(c);
    if (ev->accion == N0_ACT_SIGUIENTE) e->sel = (int16_t)((e->sel + 1) % 4);
    if (ev->accion == N0_ACT_ANTERIOR) e->sel = (int16_t)((e->sel + 3) % 4);
    return N0_OK;
}

static n0_err_t pintar(n0_ctx_t *c, n0_vista_t *v) {
    estado_t *e = (estado_t *)c->estado;
    int16_t p = n0_pantalla(v, "Estado");
    int16_t l = n0_lista(v, p, e->sel, 0, 1);
    n0_fila(v, l, "Batería", e->bateria, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Sincronización", e->sync, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Reloj", e->reloj, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Último fallo", e->fallo, N0_MARCA_NINGUNA);
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ANTERIOR, "Arriba");
    n0_accion(v, b, N0_ACT_SIGUIENTE, "Abajo");
    n0_accion(v, b, N0_ACT_ELEGIR, "Sincronizar");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
    return N0_OK;
}

const n0_app_t n0_app_estado = {
    .api_required = 1,
    .id = "estado",
    .nombre = "Estado",
    .capacidades = N0_CAP_POWER_READ | N0_CAP_SYNC_STATUS | N0_CAP_SYNC_FORCE |
                   N0_CAP_DIAG | N0_CAP_TIME_STATUS,
    .al_arrancar = arrancar,
    .al_evento = evento,
    .al_volver = NULL,
    .al_cambiar_datos = NULL,
    .al_parar = NULL,
    .pintar = pintar,
};
