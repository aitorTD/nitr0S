#include "almacen.h"

#include <string.h>

// Campos del ítem de lista. Los asigna el esquema del manifiesto; aquí se
// nombran para que el código no lleve números sueltos.
#define CAMPO_TEXTO 1
#define CAMPO_HECHO 2
#define TIPO_ITEM 10

static n0_almacen_t *g_al;  // el almacén del sistema

void n0_almacen_activar(n0_almacen_t *a) { g_al = a; }

void n0_almacen_init(n0_almacen_t *a, uint8_t *log, size_t cap, uint64_t device_id,
                     n0_dueno_t (*dueno)(uint16_t), uint8_t *valores, size_t val_cap) {
    memset(a, 0, sizeof(*a));
    a->log = log;
    a->cap = cap;
    a->device_id = device_id;
    n0_fusion_init(&a->fus, dueno, valores, val_cap);
}

size_t n0_almacen_cargar(n0_almacen_t *a, const uint8_t *bytes, size_t n) {
    // Pasada de recuperación PRIMERO: lo que haya después del último registro
    // válido se descarta, que es el caso de la tarjeta sacada a mitad.
    size_t valido = n0_op_recuperar(bytes, n, NULL, NULL);
    if (valido > a->cap) valido = a->cap;
    memcpy(a->log, bytes, valido);
    a->n = valido;
    n0_fusion_aplicar_log(&a->fus, a->log, a->n);

    // El lamport y el contador de creación se reconstruyen del registro: no
    // hace falta persistirlos aparte, y así no pueden quedar por detrás.
    size_t off = 0;
    n0_op_t op;
    while (off < a->n) {
        size_t k = 0;
        if (n0_op_decodificar(a->log + off, a->n - off, &op, &k) != N0_DEC_OK) break;
        if (op.autor == a->device_id && op.lamport > a->lamport) a->lamport = op.lamport;
        if (op.entidad.creador == a->device_id && op.entidad.seq > a->creator_seq)
            a->creator_seq = op.entidad.seq;
        off += k;
    }
    return valido;
}

// emitir escribe la operación en el registro y la fusiona. Si no cabe, NO
// escribe a medias: o entra entera o no entra.
static n0_err_t emitir(n0_almacen_t *a, uint8_t kind, n0_entidad_t e, uint16_t tipo,
                       uint16_t campo, const void *valor, uint16_t n) {
    if (n > N0_MAX_VALOR) return N0_ERR_ARG;

    n0_op_t op;
    memset(&op, 0, sizeof(op));
    op.schema = N0_OP_SCHEMA;
    op.kind = kind;
    op.autor = a->device_id;
    op.autor_tipo = N0_AUTOR_APARATO;
    op.lamport = ++a->lamport;  // monótono, nunca se reinicia
    op.reloj_pared = 0;         // el aparato puede no tener hora; no ordena nada
    op.tipo_entidad = tipo;
    op.entidad = e;
    op.campo = campo;
    op.n_valor = n;
    if (n && valor) memcpy(op.valor, valor, n);

    if (kind != N0_OP_CREAR) {
        // La versión se calcula desde el registro, que es la verdad, y no desde
        // ningún contador que pudiera quedar desincronizado.
        uint16_t c = (kind == N0_OP_BORRAR) ? N0_CAMPO_BORRADO : campo;
        const n0_ent_t *ent = n0_fusion_entidad(&a->fus, e);
        const n0_campo_t *prev = ent ? n0_ent_campo(ent, c) : NULL;
        op.version_campo = prev ? prev->version + 1 : 1;
    }

    size_t escrito = n0_op_codificar(a->log + a->n, a->cap - a->n, &op);
    if (escrito == 0) {
        a->lleno = true;
        a->lamport--;  // no se gastó: la operación no existe
        return N0_ERR_CUOTA;
    }
    a->n += escrito;
    n0_fusion_aplicar(&a->fus, &op);
    return N0_OK;
}

n0_entidad_t n0_almacen_crear(n0_almacen_t *a, uint16_t tipo) {
    n0_entidad_t e = {a->device_id, ++a->creator_seq};
    if (emitir(a, N0_OP_CREAR, e, tipo, 0, NULL, 0) != N0_OK) {
        a->creator_seq--;
        e.seq = 0;
    }
    return e;
}

n0_err_t n0_almacen_fijar(n0_almacen_t *a, n0_entidad_t e, uint16_t tipo,
                          uint16_t campo, const void *valor, uint16_t n) {
    return emitir(a, N0_OP_FIJAR, e, tipo, campo, valor, n);
}

n0_err_t n0_almacen_borrar(n0_almacen_t *a, n0_entidad_t e, uint16_t tipo) {
    uint8_t uno = 1;
    return emitir(a, N0_OP_BORRAR, e, tipo, N0_CAMPO_BORRADO, &uno, 1);
}

void n0_almacen_recibir(n0_almacen_t *a, const uint8_t *log, size_t n) {
    // Lo que viene del servidor se fusiona pero NO entra en el registro de
    // salida: cada lado es dueño de su propio registro.
    n0_fusion_aplicar_log(&a->fus, log, n);
}

// --- La tabla de servicios ---------------------------------------------------

typedef struct {
    int i;
    uint16_t tipo;
    char texto[96];
} iter_t;

static iter_t g_iter;  // una app a la vez en el aparato

static n0_err_t sv_abrir(n0_ctx_t *c, uint32_t ds, void **it) {
    if (!(c->capacidades & N0_CAP_STORE)) return N0_ERR_SIN_CAP;
    if (!g_al) return N0_ERR_ESTADO;
    (void)ds;
    g_iter.i = 0;
    g_iter.tipo = TIPO_ITEM;
    *it = &g_iter;
    return N0_OK;
}

static n0_err_t sv_siguiente(n0_ctx_t *c, void *it, const char **texto, n0_id_t *id, uint8_t *marca) {
    (void)c;
    iter_t *s = (iter_t *)it;
    while (s->i < g_al->fus.n) {
        const n0_ent_t *e = &g_al->fus.ents[s->i++];
        if (e->tipo != s->tipo) continue;
        // Un borrado es un valor, no una ausencia: aquí se usa para NO
        // mostrarlo, pero sus hechos siguen en el registro.
        if (n0_ent_borrada(&g_al->fus, e)) continue;
        uint16_t nt = 0;
        const uint8_t *t = n0_ent_valor(&g_al->fus, e, CAMPO_TEXTO, &nt);
        if (!t) continue;
        uint16_t n = nt < sizeof(s->texto) - 1 ? nt : (uint16_t)(sizeof(s->texto) - 1);
        memcpy(s->texto, t, n);
        s->texto[n] = 0;
        *texto = s->texto;
        id->creador = e->id.creador;
        id->seq = e->id.seq;
        uint16_t nh = 0;
        const uint8_t *h = n0_ent_valor(&g_al->fus, e, CAMPO_HECHO, &nh);
        *marca = (h && nh > 0 && h[0] == 1) ? N0_MARCA_HECHO : N0_MARCA_NINGUNA;
        return N0_OK;
    }
    return N0_ERR_NO_ENCONTRADO;
}

static n0_err_t sv_cerrar(n0_ctx_t *c, void *it) {
    (void)c;
    (void)it;
    return N0_OK;
}

static n0_err_t sv_marcar(n0_ctx_t *c, uint32_t ds, n0_id_t id, bool hecho) {
    if (!(c->capacidades & N0_CAP_STORE)) return N0_ERR_SIN_CAP;
    if (!g_al) return N0_ERR_ESTADO;
    (void)ds;
    uint8_t v = hecho ? 1 : 0;
    n0_entidad_t e = {id.creador, id.seq};
    return n0_almacen_fijar(g_al, e, TIPO_ITEM, CAMPO_HECHO, &v, 1);
}

static n0_err_t sv_sync_estado(n0_ctx_t *c, n0_sync_estado_t *out) {
    if (!(c->capacidades & N0_CAP_SYNC_STATUS)) return N0_ERR_SIN_CAP;
    *out = N0_SYNC_PENDIENTE;
    return N0_OK;
}

static n0_err_t sv_reloj(n0_ctx_t *c, n0_reloj_estado_t *out) {
    if (!(c->capacidades & N0_CAP_TIME_STATUS)) return N0_ERR_SIN_CAP;
    *out = N0_RELOJ_DUDOSO;  // hasta que haya RTC de verdad, se dice la verdad
    return N0_OK;
}

const n0_servicios_t N0_SERVICIOS_ALMACEN = {
    .lista_abrir = sv_abrir,
    .lista_siguiente = sv_siguiente,
    .lista_cerrar = sv_cerrar,
    .marcar = sv_marcar,
    .sync_estado = sv_sync_estado,
    .sync_forzar = NULL,
    .bateria = NULL,
    .reloj_estado = sv_reloj,
    .diag = NULL,
};
