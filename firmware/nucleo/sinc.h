// sinc — el sobre y el ciclo de sincronización, lado del aparato.
//
// MISMO SOBRE, byte a byte, que internal/wire del servidor.
//
// Las apps NO ven nada de esto: escriben en el almacén, el almacén añade al
// registro, y esto lo drena. Exponerlo invertiría la dependencia y destruiría
// en silencio el funcionamiento sin red, porque una app que se olvidara de
// llamarlo perdería datos sin avisar.
#ifndef NITR0S_SINC_H
#define NITR0S_SINC_H

#include "almacen.h"
#include "sha256.h"

#define N0_SOBRE_MAGIC "nitr0Sop"
#define N0_SOBRE_CAB 47
#define N0_PROTO_VERSION 1
#define N0_MAX_SECRETO 64

typedef struct {
    uint16_t proto;
    uint64_t dataset;
    uint64_t emisor;
    uint8_t emisor_tipo;
    uint64_t sobre_seq;
    uint32_t n_ops;
    uint64_t confirmado_hasta;
} n0_sobre_t;

// Empaqueta y firma. Devuelve los bytes escritos, o 0 si no cabe.
size_t n0_sobre_empaquetar(uint8_t *dst, size_t cap, const n0_sobre_t *s,
                           const uint8_t *ops, size_t n_ops_bytes,
                           const uint8_t *secreto, size_t n_secreto);

typedef enum {
    N0_SOBRE_OK = 0,
    N0_SOBRE_CORTO,
    N0_SOBRE_MAGIC_MAL,
    N0_SOBRE_FIRMA,
} n0_sobre_err_t;

// Verifica la firma y devuelve dónde empiezan las operaciones.
n0_sobre_err_t n0_sobre_desempaquetar(const uint8_t *src, size_t n, n0_sobre_t *s,
                                      const uint8_t **ops, size_t *n_ops_bytes,
                                      const uint8_t *secreto, size_t n_secreto);

// El transporte es una costura: en el simulador son sockets, en el aparato
// será esp_http_client. El ciclo de sincronización no sabe cuál es.
typedef struct {
    // Devuelve 0 si la petición se hizo; *http lleva el código. Un fallo de
    // red devuelve != 0, que NO es lo mismo que un 401.
    int (*post)(void *ctx, const char *ruta, const uint8_t *cuerpo, size_t n,
                uint8_t *resp, size_t cap, size_t *n_resp, int *http);
    void *ctx;
} n0_transporte_t;

typedef struct {
    uint64_t device_id;
    uint64_t dataset;
    uint8_t secreto[N0_MAX_SECRETO];
    size_t n_secreto;

    uint64_t sobre_seq;       // antirreplay: estrictamente creciente, se persiste
    size_t enviado_bytes;     // cuánto del registro propio ha aceptado el servidor
    uint64_t recibido_hasta;  // lamport del flujo DEL SERVIDOR que ya tenemos

    // Diagnóstico, que es lo que la app de Estado enseña.
    uint32_t fallos_seguidos;
    int ultimo_http;
    int64_t hora_servidor;  // la que vino en el último 401, para ajustar el reloj
} n0_sinc_t;

void n0_sinc_init(n0_sinc_t *s, uint64_t device_id, uint64_t dataset,
                  const uint8_t *secreto, size_t n_secreto);

typedef enum {
    N0_SINC_OK = 0,
    N0_SINC_SIN_RED,
    N0_SINC_RECHAZADO,   // 4xx: el cuerpo dice por qué
    N0_SINC_NO_AUTORIZADO,
    N0_SINC_RESPUESTA_MALA,
} n0_sinc_res_t;

const char *n0_sinc_str(n0_sinc_res_t r);

// Un ciclo: sube lo pendiente, baja lo que falte y lo fusiona.
n0_sinc_res_t n0_sinc_ciclo(n0_sinc_t *s, n0_almacen_t *a, const n0_transporte_t *t);

#endif
