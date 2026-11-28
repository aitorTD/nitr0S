#include "sinc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void p16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *b, uint32_t v) { for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i)); }
static void p64(uint8_t *b, uint64_t v) { for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i)); }
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

size_t n0_sobre_empaquetar(uint8_t *dst, size_t cap, const n0_sobre_t *s,
                           const uint8_t *ops, size_t n_ops_bytes,
                           const uint8_t *secreto, size_t n_secreto) {
    size_t total = N0_SOBRE_CAB + n_ops_bytes + N0_SHA256_LEN;
    if (cap < total) return 0;
    memcpy(dst, N0_SOBRE_MAGIC, 8);
    p16(dst + 8, s->proto);
    p64(dst + 10, s->dataset);
    p64(dst + 18, s->emisor);
    dst[26] = s->emisor_tipo;
    p64(dst + 27, s->sobre_seq);
    p32(dst + 35, s->n_ops);
    p64(dst + 39, s->confirmado_hasta);
    if (n_ops_bytes) memcpy(dst + N0_SOBRE_CAB, ops, n_ops_bytes);
    n0_hmac_sha256(secreto, n_secreto, dst, N0_SOBRE_CAB + n_ops_bytes,
                   dst + N0_SOBRE_CAB + n_ops_bytes);
    return total;
}

n0_sobre_err_t n0_sobre_desempaquetar(const uint8_t *src, size_t n, n0_sobre_t *s,
                                      const uint8_t **ops, size_t *n_ops_bytes,
                                      const uint8_t *secreto, size_t n_secreto) {
    if (n < N0_SOBRE_CAB + N0_SHA256_LEN) return N0_SOBRE_CORTO;
    if (memcmp(src, N0_SOBRE_MAGIC, 8) != 0) return N0_SOBRE_MAGIC_MAL;

    size_t cuerpo = n - N0_SHA256_LEN;
    uint8_t mac[N0_SHA256_LEN];
    n0_hmac_sha256(secreto, n_secreto, src, cuerpo, mac);
    // En tiempo constante: comparar un MAC con memcmp filtra por tiempo.
    if (!n0_iguales_ct(mac, src + cuerpo, N0_SHA256_LEN)) return N0_SOBRE_FIRMA;

    s->proto = g16(src + 8);
    s->dataset = g64(src + 10);
    s->emisor = g64(src + 18);
    s->emisor_tipo = src[26];
    s->sobre_seq = g64(src + 27);
    s->n_ops = g32(src + 35);
    s->confirmado_hasta = g64(src + 39);
    *ops = src + N0_SOBRE_CAB;
    *n_ops_bytes = cuerpo - N0_SOBRE_CAB;
    return N0_SOBRE_OK;
}

void n0_sinc_init(n0_sinc_t *s, uint64_t device_id, uint64_t dataset,
                  const uint8_t *secreto, size_t n_secreto) {
    memset(s, 0, sizeof(*s));
    s->device_id = device_id;
    s->dataset = dataset;
    s->n_secreto = n_secreto > N0_MAX_SECRETO ? N0_MAX_SECRETO : n_secreto;
    memcpy(s->secreto, secreto, s->n_secreto);
}

const char *n0_sinc_str(n0_sinc_res_t r) {
    switch (r) {
        case N0_SINC_OK: return "ok";
        case N0_SINC_SIN_RED: return "sin red";
        case N0_SINC_RECHAZADO: return "el servidor rechazó el lote";
        case N0_SINC_NO_AUTORIZADO: return "no autorizado";
        case N0_SINC_RESPUESTA_MALA: return "respuesta ilegible";
    }
    return "?";
}

// Cuenta cuántas operaciones hay en un trozo de registro.
static uint32_t contar(const uint8_t *log, size_t n) {
    int ops = 0;
    n0_op_recuperar(log, n, &ops, NULL);
    return (uint32_t)ops;
}

n0_sinc_res_t n0_sinc_ciclo(n0_sinc_t *s, n0_almacen_t *a, const n0_transporte_t *t) {
    static uint8_t pet[96 * 1024];
    static uint8_t resp[96 * 1024];

    // Lo pendiente de subir: la cola del registro propio que el servidor
    // todavía no ha aceptado. Es un desplazamiento, así que reenviar desde ahí
    // siempre es seguro y un corte a mitad no necesita lógica especial.
    size_t desde = s->enviado_bytes <= a->n ? s->enviado_bytes : 0;
    const uint8_t *pendiente = a->log + desde;
    size_t n_pend = a->n - desde;

    n0_sobre_t sob;
    memset(&sob, 0, sizeof(sob));
    sob.proto = N0_PROTO_VERSION;
    sob.dataset = s->dataset;
    sob.emisor = s->device_id;
    sob.emisor_tipo = N0_AUTOR_APARATO;
    // Estrictamente creciente, también en un reintento: el antirreplay lo
    // exige, y la idempotencia por op_id impide que el reintento duplique.
    sob.sobre_seq = ++s->sobre_seq;
    sob.n_ops = contar(pendiente, n_pend);
    sob.confirmado_hasta = s->recibido_hasta;

    size_t n_pet = n0_sobre_empaquetar(pet, sizeof(pet), &sob, pendiente, n_pend,
                                       s->secreto, s->n_secreto);
    if (n_pet == 0) return N0_SINC_RESPUESTA_MALA;

    size_t n_resp = 0;
    int http = 0;
    if (t->post(t->ctx, "/v1/sync/ops", pet, n_pet, resp, sizeof(resp), &n_resp, &http) != 0) {
        s->fallos_seguidos++;
        return N0_SINC_SIN_RED;
    }
    s->ultimo_http = http;

    if (http == 401) {
        // El cuerpo lleva la hora del servidor: el aparato ajusta su reloj y
        // reintenta EXACTAMENTE una vez. Sin esto, un reloj desviado deja el
        // aparato bloqueado para siempre y sin teclado donde arreglarlo.
        resp[n_resp < sizeof(resp) ? n_resp : sizeof(resp) - 1] = 0;
        const char *p = strstr((const char *)resp, "\"server_time\":");
        if (p) s->hora_servidor = (int64_t)strtoll(p + 14, NULL, 10);
        s->fallos_seguidos++;
        return N0_SINC_NO_AUTORIZADO;
    }
    if (http != 200) {
        s->fallos_seguidos++;
        return N0_SINC_RECHAZADO;
    }

    const uint8_t *ops = NULL;
    size_t n_ops = 0;
    n0_sobre_t rsob;
    if (n0_sobre_desempaquetar(resp, n_resp, &rsob, &ops, &n_ops,
                               s->secreto, s->n_secreto) != N0_SOBRE_OK) {
        s->fallos_seguidos++;
        return N0_SINC_RESPUESTA_MALA;
    }

    // Lo que viene del servidor se fusiona pero NO entra en el registro de
    // salida: cada lado es dueño de su propio registro.
    n0_almacen_recibir(a, ops, n_ops);

    // Marca de agua del flujo del servidor: el lamport contiguo más alto que
    // ya tenemos.
    size_t off = 0;
    static n0_op_t op;
    while (off < n_ops) {
        size_t k = 0;
        if (n0_op_decodificar(ops + off, n_ops - off, &op, &k) != N0_DEC_OK) break;
        if (op.lamport > s->recibido_hasta) s->recibido_hasta = op.lamport;
        off += k;
    }

    s->enviado_bytes = a->n;
    s->fallos_seguidos = 0;
    return N0_SINC_OK;
}
