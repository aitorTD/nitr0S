#include "opfmt.h"

#include <string.h>

// CRC-32 (IEEE, reflejado), el mismo que hash/crc32.ChecksumIEEE de Go.
// Sin tabla precalculada: 256 entradas son 1 KB de flash y esto se ejecuta
// unas pocas veces por operación.
uint32_t n0_crc32(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static void p16(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)(v >> 8);
}
static void p32(uint8_t *b, uint32_t v) {
    for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i));
}
static void p64(uint8_t *b, uint64_t v) {
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
}
static uint16_t g16(const uint8_t *b) { return (uint16_t)(b[0] | ((uint16_t)b[1] << 8)); }
static uint32_t g32(const uint8_t *b) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)b[i] << (8 * i);
    return v;
}
static uint64_t g64(const uint8_t *b) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)b[i] << (8 * i);
    return v;
}

size_t n0_op_codificar(uint8_t *dst, size_t cap, const n0_op_t *op) {
    if (op->n_valor > N0_MAX_VALOR) return 0;
    size_t carga = N0_OP_CARGA + op->n_valor;
    size_t total = 4 + carga + 4;
    if (cap < total) return 0;

    p32(dst, (uint32_t)carga);
    uint8_t *p = dst + 4;
    p[0] = op->schema;
    p[1] = op->kind;
    p64(&p[2], op->autor);
    p[10] = op->autor_tipo;
    p64(&p[11], op->lamport);
    p64(&p[19], (uint64_t)op->reloj_pared);
    p16(&p[27], op->tipo_entidad);
    p64(&p[29], op->entidad.creador);
    p64(&p[37], op->entidad.seq);
    p16(&p[45], op->campo);
    p64(&p[47], op->version_campo);
    p16(&p[55], op->n_valor);
    if (op->n_valor) memcpy(&p[57], op->valor, op->n_valor);
    p32(dst + 4 + carga, n0_crc32(dst, 4 + carga));
    return total;
}

n0_dec_t n0_op_decodificar(const uint8_t *src, size_t n, n0_op_t *op, size_t *consumido) {
    *consumido = 0;
    if (n < 8) return N0_DEC_CORTO;
    uint32_t carga = g32(src);
    if (carga < N0_OP_CARGA || carga > N0_OP_CARGA + N0_MAX_VALOR) return N0_DEC_CORTO;
    size_t total = 4 + (size_t)carga + 4;
    if (n < total) return N0_DEC_CORTO;
    if (n0_crc32(src, 4 + (size_t)carga) != g32(src + 4 + carga)) return N0_DEC_CRC;

    const uint8_t *p = src + 4;
    memset(op, 0, sizeof(*op));
    op->schema = p[0];
    *consumido = total;
    // Un schema más nuevo se GUARDA igual y se marca, porque perderlo sería
    // perder un hecho; aquí se avisa a quien llama y se consume entero.
    if (op->schema > N0_OP_SCHEMA) return N0_DEC_SCHEMA;

    op->kind = p[1];
    op->autor = g64(&p[2]);
    op->autor_tipo = p[10];
    op->lamport = g64(&p[11]);
    op->reloj_pared = (int64_t)g64(&p[19]);
    op->tipo_entidad = g16(&p[27]);
    op->entidad.creador = g64(&p[29]);
    op->entidad.seq = g64(&p[37]);
    op->campo = g16(&p[45]);
    op->version_campo = g64(&p[47]);
    op->n_valor = g16(&p[55]);
    if ((size_t)N0_OP_CARGA + op->n_valor > carga) {
        *consumido = 0;
        return N0_DEC_CORTO;
    }
    if (op->n_valor) memcpy(op->valor, &p[57], op->n_valor);
    return N0_DEC_OK;
}

size_t n0_op_recuperar(const uint8_t *log, size_t n, int *n_ops, int *no_interpretadas) {
    size_t off = 0, valido = 0;
    if (n_ops) *n_ops = 0;
    if (no_interpretadas) *no_interpretadas = 0;
    n0_op_t op;
    while (off < n) {
        size_t k = 0;
        n0_dec_t d = n0_op_decodificar(log + off, n - off, &op, &k);
        if (d == N0_DEC_SCHEMA) {
            if (no_interpretadas) (*no_interpretadas)++;
            off += k;
            valido = off;
            continue;
        }
        if (d != N0_DEC_OK) break;
        if (n_ops) (*n_ops)++;
        off += k;
        valido = off;
    }
    return valido;
}
