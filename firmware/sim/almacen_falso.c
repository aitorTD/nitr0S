// Almacén de mentira para el simulador.
//
// Es un sustituto temporal del almacén de verdad, que hablará el formato del
// registro de operaciones de diseno/registro-de-operaciones-y-sincronizacion.md
// y sincronizará con el servidor. Lo único que hace falta hoy es mover las
// apps y poder verlas.
#include <stdio.h>
#include <string.h>

#include "../nucleo/app.h"

#define MAX_ITEMS 24

typedef struct {
    uint32_t id;
    char texto[64];
    uint8_t marca;
} item_t;

static item_t g_items[MAX_ITEMS];
static int g_n;
static int g_cursor;
static n0_sync_estado_t g_sync = N0_SYNC_PENDIENTE;
static n0_reloj_estado_t g_reloj = N0_RELOJ_FIABLE;
static int g_sync_forzados;

void almacen_reset(void) {
    g_n = 0;
    g_sync_forzados = 0;
}

void almacen_poner(uint32_t id, const char *texto, uint8_t marca) {
    if (g_n >= MAX_ITEMS) return;
    g_items[g_n].id = id;
    snprintf(g_items[g_n].texto, sizeof(g_items[g_n].texto), "%s", texto);
    g_items[g_n].marca = marca;
    g_n++;
}

void almacen_reloj(n0_reloj_estado_t r) { g_reloj = r; }
void almacen_sync(n0_sync_estado_t s) { g_sync = s; }
int almacen_sync_forzados(void) { return g_sync_forzados; }

static n0_err_t lista_abrir(n0_ctx_t *c, uint32_t ds, void **it) {
    if (!(c->capacidades & N0_CAP_STORE)) return N0_ERR_SIN_CAP;
    (void)ds;
    g_cursor = 0;
    *it = &g_cursor;
    return N0_OK;
}

static n0_err_t lista_siguiente(n0_ctx_t *c, void *it, const char **texto, uint32_t *id, uint8_t *marca) {
    (void)c;
    int *cur = (int *)it;
    if (*cur >= g_n) return N0_ERR_NO_ENCONTRADO;
    *texto = g_items[*cur].texto;
    *id = g_items[*cur].id;
    *marca = g_items[*cur].marca;
    (*cur)++;
    return N0_OK;
}

static n0_err_t lista_cerrar(n0_ctx_t *c, void *it) {
    (void)c;
    (void)it;
    return N0_OK;
}

static n0_err_t marcar(n0_ctx_t *c, uint32_t ds, uint32_t id, bool hecho) {
    if (!(c->capacidades & N0_CAP_STORE)) return N0_ERR_SIN_CAP;
    (void)ds;
    for (int i = 0; i < g_n; i++) {
        if (g_items[i].id == id) {
            g_items[i].marca = hecho ? N0_MARCA_HECHO : N0_MARCA_NINGUNA;
            return N0_OK;
        }
    }
    return N0_ERR_NO_ENCONTRADO;
}

static n0_err_t sync_estado(n0_ctx_t *c, n0_sync_estado_t *out) {
    if (!(c->capacidades & N0_CAP_SYNC_STATUS)) return N0_ERR_SIN_CAP;
    *out = g_sync;
    return N0_OK;
}

static n0_err_t sync_forzar(n0_ctx_t *c) {
    if (!(c->capacidades & N0_CAP_SYNC_FORCE)) return N0_ERR_SIN_CAP;
    g_sync_forzados++;
    g_sync = N0_SYNC_AL_DIA;
    return N0_OK;
}

static n0_err_t bateria(n0_ctx_t *c, int *pct, int *mv) {
    if (!(c->capacidades & N0_CAP_POWER_READ)) return N0_ERR_SIN_CAP;
    *pct = 72;
    *mv = 3894;
    return N0_OK;
}

static n0_err_t reloj_estado(n0_ctx_t *c, n0_reloj_estado_t *out) {
    if (!(c->capacidades & N0_CAP_TIME_STATUS)) return N0_ERR_SIN_CAP;
    *out = g_reloj;
    return N0_OK;
}

static n0_err_t diag(n0_ctx_t *c, const char **ultimo, uint32_t *reinicios) {
    if (!(c->capacidades & N0_CAP_DIAG)) return N0_ERR_SIN_CAP;
    *ultimo = "ninguno";
    *reinicios = 0;
    return N0_OK;
}

const n0_servicios_t ALMACEN_FALSO = {
    .lista_abrir = lista_abrir,
    .lista_siguiente = lista_siguiente,
    .lista_cerrar = lista_cerrar,
    .marcar = marcar,
    .sync_estado = sync_estado,
    .sync_forzar = sync_forzar,
    .bateria = bateria,
    .reloj_estado = reloj_estado,
    .diag = diag,
};
