#include "fusion.h"

#include <string.h>

void n0_fusion_init(n0_fusion_t *f, n0_dueno_t (*dueno)(uint16_t)) {
    memset(f, 0, sizeof(*f));
    f->dueno = dueno;
}

static int rango_lado(n0_dueno_t d, uint8_t autor_tipo) {
    switch (d) {
        case N0_DUENO_APARATO:
        case N0_DUENO_COMPARTIDO:
            // Para compartido gana el aparato: ahí vive "gana el dispositivo",
            // y SOLO ahí, como desempate cuando el tiempo lógico empata.
            return autor_tipo == N0_AUTOR_APARATO ? 1 : 0;
        case N0_DUENO_SERVIDOR:
            return (autor_tipo == N0_AUTOR_CONSOLA || autor_tipo == N0_AUTOR_AGENTE) ? 1 : 0;
    }
    return 0;
}

// Orden total estricto: garantiza convergencia sea cual sea el orden de
// llegada. Las mismas cuatro reglas que el servidor, en el mismo orden.
static bool gana(const n0_op_t *a, const n0_op_t *b, n0_dueno_t d, const char **motivo) {
    if (a->version_campo != b->version_campo) {
        *motivo = "versión de campo mayor";
        return a->version_campo > b->version_campo;
    }
    int ra = rango_lado(d, a->autor_tipo), rb = rango_lado(d, b->autor_tipo);
    if (ra != rb) {
        *motivo = "empate de versión: gana el lado dueño de la entidad";
        return ra > rb;
    }
    if (a->autor != b->autor) {
        *motivo = "empate de versión y de lado: gana el autor menor";
        return a->autor < b->autor;
    }
    *motivo = "empate total: gana el lamport menor";
    return a->lamport < b->lamport;
}

static n0_ent_t *buscar_o_crear(n0_fusion_t *f, n0_entidad_t id, uint16_t tipo) {
    for (int i = 0; i < f->n; i++)
        if (n0_entidad_igual(f->ents[i].id, id)) return &f->ents[i];
    if (f->n >= N0_MAX_ENTIDADES) {
        f->desbordado = true;
        return NULL;
    }
    n0_ent_t *e = &f->ents[f->n++];
    memset(e, 0, sizeof(*e));
    e->id = id;
    e->tipo = tipo;
    return e;
}

static void anotar_conflicto(n0_fusion_t *f, const n0_ent_t *e, uint16_t campo,
                             const n0_op_t *perdedor, const char *motivo) {
    if (f->n_conflictos >= N0_MAX_CONFLICTOS) return;
    n0_conflicto_t *c = &f->conflictos[f->n_conflictos++];
    c->entidad = e->id;
    c->campo = campo;
    c->autor_perdedor = perdedor->autor_tipo;
    c->n_perdedor = perdedor->n_valor > (uint16_t)sizeof(c->perdedor)
                        ? (uint16_t)sizeof(c->perdedor)
                        : perdedor->n_valor;
    memcpy(c->perdedor, perdedor->valor, c->n_perdedor);
    c->motivo = motivo;
}

void n0_fusion_aplicar(n0_fusion_t *f, const n0_op_t *op) {
    n0_ent_t *e = buscar_o_crear(f, op->entidad, op->tipo_entidad);
    if (!e) return;

    if (op->kind == N0_OP_CREAR) {
        // Las creaciones nunca son conflicto. Esta única línea es la que salva
        // las escrituras del asistente.
        if (!e->creada) {
            e->creada = true;
            e->op_crear = *op;
            e->tipo = op->tipo_entidad;
        }
        return;
    }

    uint16_t campo = op->campo;
    n0_op_t ajustada = *op;
    if (op->kind == N0_OP_BORRAR) {
        campo = N0_CAMPO_BORRADO;
        ajustada.campo = campo;
        if (ajustada.n_valor == 0) {
            ajustada.n_valor = 1;
            ajustada.valor[0] = 1;
        }
    }

    n0_dueno_t d = f->dueno ? f->dueno(e->tipo) : N0_DUENO_COMPARTIDO;
    n0_campo_t *slot = NULL;
    for (int i = 0; i < e->n_campos; i++)
        if (e->campos[i].usado && e->campos[i].campo == campo) slot = &e->campos[i];

    if (!slot) {
        if (e->n_campos >= N0_MAX_CAMPOS) {
            f->desbordado = true;
            return;
        }
        slot = &e->campos[e->n_campos++];
        slot->campo = campo;
        slot->usado = true;
        slot->op = ajustada;
        return;
    }

    const char *motivo = "";
    bool nueva_gana = gana(&ajustada, &slot->op, d, &motivo);
    const n0_op_t *ganador = nueva_gana ? &ajustada : &slot->op;
    const n0_op_t *perdedor = nueva_gana ? &slot->op : &ajustada;
    if (!nueva_gana) (void)gana(&slot->op, &ajustada, d, &motivo);

    // Solo es conflicto si se pierde una escritura DISTINTA de OTRO autor. Dos
    // borrados que convergen no son nada que contar, y que un autor sobrescriba
    // su propio valor anterior es una edición, no un conflicto.
    bool distinto = ganador->n_valor != perdedor->n_valor ||
                    memcmp(ganador->valor, perdedor->valor, ganador->n_valor) != 0;
    if (distinto && ganador->autor != perdedor->autor)
        anotar_conflicto(f, e, campo, perdedor, motivo);

    if (nueva_gana) slot->op = ajustada;
}

size_t n0_fusion_aplicar_log(n0_fusion_t *f, const uint8_t *log, size_t n) {
    size_t off = 0;
    int aplicadas = 0;
    n0_op_t op;
    while (off < n) {
        size_t k = 0;
        n0_dec_t d = n0_op_decodificar(log + off, n - off, &op, &k);
        if (d == N0_DEC_SCHEMA) {
            off += k;
            continue;
        }
        if (d != N0_DEC_OK) break;
        n0_fusion_aplicar(f, &op);
        aplicadas++;
        off += k;
    }
    return (size_t)aplicadas;
}

const n0_ent_t *n0_fusion_entidad(const n0_fusion_t *f, n0_entidad_t id) {
    for (int i = 0; i < f->n; i++)
        if (n0_entidad_igual(f->ents[i].id, id)) return &f->ents[i];
    return NULL;
}

const n0_op_t *n0_ent_campo(const n0_ent_t *e, uint16_t campo) {
    for (int i = 0; i < e->n_campos; i++)
        if (e->campos[i].usado && e->campos[i].campo == campo) return &e->campos[i].op;
    return NULL;
}

bool n0_ent_borrada(const n0_ent_t *e) {
    const n0_op_t *op = n0_ent_campo(e, N0_CAMPO_BORRADO);
    return op && op->n_valor > 0 && op->valor[0] == 1;
}

static int menor(n0_entidad_t a, n0_entidad_t b) {
    if (a.creador != b.creador) return a.creador < b.creador;
    return a.seq < b.seq;
}

void n0_fusion_ordenar(n0_fusion_t *f) {
    for (int i = 1; i < f->n; i++) {
        n0_ent_t tmp = f->ents[i];
        int j = i - 1;
        while (j >= 0 && menor(tmp.id, f->ents[j].id)) {
            f->ents[j + 1] = f->ents[j];
            j--;
        }
        f->ents[j + 1] = tmp;
    }
    // Y los campos dentro de cada entidad, por identificador.
    for (int i = 0; i < f->n; i++) {
        n0_ent_t *e = &f->ents[i];
        for (int a = 1; a < e->n_campos; a++) {
            n0_campo_t t = e->campos[a];
            int b = a - 1;
            while (b >= 0 && e->campos[b].campo > t.campo) {
                e->campos[b + 1] = e->campos[b];
                b--;
            }
            e->campos[b + 1] = t;
        }
    }
}
