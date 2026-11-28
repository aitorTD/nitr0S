#include "fusion.h"

#include <string.h>

void n0_fusion_init(n0_fusion_t *f, n0_dueno_t (*dueno)(uint16_t),
                    uint8_t *valores, size_t val_cap) {
    memset(f, 0, sizeof(*f));
    f->dueno = dueno;
    f->valores = valores;
    f->val_cap = val_cap;
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

// Orden total estricto: converge sea cual sea el orden de llegada. Las mismas
// cuatro reglas que el servidor, en el mismo orden.
static bool gana_meta(uint64_t ver_a, uint8_t at_a, uint64_t aut_a, uint64_t lam_a,
                      uint64_t ver_b, uint8_t at_b, uint64_t aut_b, uint64_t lam_b,
                      n0_dueno_t d, const char **motivo) {
    if (ver_a != ver_b) {
        *motivo = "versión de campo mayor";
        return ver_a > ver_b;
    }
    int ra = rango_lado(d, at_a), rb = rango_lado(d, at_b);
    if (ra != rb) {
        *motivo = "empate de versión: gana el lado dueño de la entidad";
        return ra > rb;
    }
    if (aut_a != aut_b) {
        *motivo = "empate de versión y de lado: gana el autor menor";
        return aut_a < aut_b;
    }
    *motivo = "empate total: gana el lamport menor";
    return lam_a < lam_b;
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

// guardar_valor añade al almacén. Si el mismo valor ya está, lo reutiliza: una
// lista en la que se marca y desmarca repite muchas veces los mismos bytes.
static bool guardar_valor(n0_fusion_t *f, const uint8_t *v, uint16_t n, uint32_t *off) {
    if (n == 0) {
        *off = 0;
        return true;
    }
    if (f->val_n >= n) {
        for (size_t i = 0; i + n <= f->val_n; i++) {
            if (memcmp(f->valores + i, v, n) == 0) {
                *off = (uint32_t)i;
                return true;
            }
        }
    }
    if (f->val_n + n > f->val_cap) {
        f->desbordado = true;
        return false;
    }
    memcpy(f->valores + f->val_n, v, n);
    *off = (uint32_t)f->val_n;
    f->val_n += n;
    return true;
}

static void anotar_conflicto(n0_fusion_t *f, const n0_ent_t *e, uint16_t campo,
                             uint8_t autor_tipo, const uint8_t *v, uint16_t n,
                             const char *motivo) {
    if (f->n_conflictos >= N0_MAX_CONFLICTOS) return;
    n0_conflicto_t *c = &f->conflictos[f->n_conflictos++];
    c->entidad = e->id;
    c->campo = campo;
    c->autor_perdedor = autor_tipo;
    c->n_perdedor = n > N0_CONF_VALOR ? N0_CONF_VALOR : n;
    memcpy(c->perdedor, v, c->n_perdedor);
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
            e->crear_autor = op->autor;
            e->crear_lamport = op->lamport;
            e->tipo = op->tipo_entidad;
        }
        return;
    }

    uint16_t campo = op->campo;
    const uint8_t *val = op->valor;
    uint16_t n_val = op->n_valor;
    static const uint8_t UNO = 1;
    if (op->kind == N0_OP_BORRAR) {
        campo = N0_CAMPO_BORRADO;
        if (n_val == 0) {
            val = &UNO;
            n_val = 1;
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
        uint32_t off = 0;
        if (!guardar_valor(f, val, n_val, &off)) return;
        slot = &e->campos[e->n_campos++];
        slot->campo = campo;
        slot->usado = true;
        slot->kind = op->kind;
        slot->autor = op->autor;
        slot->autor_tipo = op->autor_tipo;
        slot->lamport = op->lamport;
        slot->version = op->version_campo;
        slot->n_valor = n_val;
        slot->off_valor = off;
        return;
    }

    const char *motivo = "";
    bool nueva_gana = gana_meta(op->version_campo, op->autor_tipo, op->autor, op->lamport,
                                slot->version, slot->autor_tipo, slot->autor, slot->lamport,
                                d, &motivo);
    if (!nueva_gana)
        (void)gana_meta(slot->version, slot->autor_tipo, slot->autor, slot->lamport,
                        op->version_campo, op->autor_tipo, op->autor, op->lamport, d, &motivo);

    const uint8_t *v_slot = f->valores + slot->off_valor;
    bool distinto = slot->n_valor != n_val || memcmp(v_slot, val, n_val) != 0;
    // Solo es conflicto si se pierde una escritura DISTINTA de OTRO autor.
    if (distinto && slot->autor != op->autor) {
        if (nueva_gana) {
            anotar_conflicto(f, e, campo, slot->autor_tipo, v_slot, slot->n_valor, motivo);
        } else {
            anotar_conflicto(f, e, campo, op->autor_tipo, val, n_val, motivo);
        }
    }

    if (nueva_gana) {
        uint32_t off = 0;
        if (!guardar_valor(f, val, n_val, &off)) return;
        slot->kind = op->kind;
        slot->autor = op->autor;
        slot->autor_tipo = op->autor_tipo;
        slot->lamport = op->lamport;
        slot->version = op->version_campo;
        slot->n_valor = n_val;
        slot->off_valor = off;
    }
}

size_t n0_fusion_aplicar_log(n0_fusion_t *f, const uint8_t *log, size_t n) {
    size_t off = 0;
    int aplicadas = 0;
    static n0_op_t op;  // 4 KB: fuera de la pila, que en el aparato es pequeña
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

const n0_campo_t *n0_ent_campo(const n0_ent_t *e, uint16_t campo) {
    for (int i = 0; i < e->n_campos; i++)
        if (e->campos[i].usado && e->campos[i].campo == campo) return &e->campos[i];
    return NULL;
}

const uint8_t *n0_ent_valor(const n0_fusion_t *f, const n0_ent_t *e, uint16_t campo, uint16_t *n) {
    const n0_campo_t *c = n0_ent_campo(e, campo);
    if (!c) {
        if (n) *n = 0;
        return NULL;
    }
    if (n) *n = c->n_valor;
    return f->valores + c->off_valor;
}

bool n0_ent_borrada(const n0_fusion_t *f, const n0_ent_t *e) {
    uint16_t n = 0;
    const uint8_t *v = n0_ent_valor(f, e, N0_CAMPO_BORRADO, &n);
    return v && n > 0 && v[0] == 1;
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
