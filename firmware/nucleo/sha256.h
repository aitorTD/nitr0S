// sha256 — SHA-256 y HMAC-SHA256.
//
// Hace falta porque EL HMAC ES EL AUTENTICADOR y el TLS es confidencialidad y
// comodidad: el TLS termina en Funnel y no en la Pi, así que sin HMAC el
// servidor no sabe de verdad quién le escribe.
//
// Implementación propia y pequeña en vez de arrastrar una biblioteca: en el
// aparato esto acabará siendo mbedTLS, que ya viene con ESP-IDF, y aquí solo
// hace falta para que el simulador hable con el servidor de verdad.
#ifndef NITR0S_SHA256_H
#define NITR0S_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define N0_SHA256_LEN 32

typedef struct {
    uint32_t h[8];
    uint64_t n_bits;
    uint8_t buf[64];
    size_t buf_n;
} n0_sha256_t;

void n0_sha256_init(n0_sha256_t *s);
void n0_sha256_add(n0_sha256_t *s, const uint8_t *d, size_t n);
void n0_sha256_fin(n0_sha256_t *s, uint8_t out[N0_SHA256_LEN]);
void n0_sha256(const uint8_t *d, size_t n, uint8_t out[N0_SHA256_LEN]);

void n0_hmac_sha256(const uint8_t *clave, size_t n_clave, const uint8_t *d, size_t n,
                    uint8_t out[N0_SHA256_LEN]);

// Comparación en tiempo constante. Comparar un MAC con memcmp filtra por
// tiempo cuántos bytes acertó quien lo intenta.
int n0_iguales_ct(const uint8_t *a, const uint8_t *b, size_t n);

#endif
