// opfmt — codificación canónica del registro de operaciones, lado del aparato.
//
// MISMO FORMATO, BYTE A BYTE, que internal/opfmt del servidor. No es una
// reimplementación parecida: si los dos lados no producen los mismos bytes, la
// decisión 11 del brief —«los mismos bytes viajan por HTTPS, en la tarjeta o
// por rsync»— deja de ser cierta y nadie se entera hasta que algo se pierde.
//
// Hay una prueba cruzada que lo comprueba: pruebas/test_cruzado.c fusiona un
// registro generado por el servidor en Go y compara el estado resultante.
#ifndef NITR0S_OPFMT_H
#define NITR0S_OPFMT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define N0_OP_SCHEMA 1
#define N0_CAMPO_BORRADO 0  // el campo reservado que hace del borrado un valor

typedef enum { N0_OP_CREAR = 1, N0_OP_FIJAR = 2, N0_OP_BORRAR = 3 } n0_op_kind_t;
typedef enum { N0_AUTOR_APARATO = 1, N0_AUTOR_CONSOLA = 2, N0_AUTOR_AGENTE = 3 } n0_autor_t;

#define N0_MAX_VALOR 4096
#define N0_OP_CARGA 57  // tamaño fijo de la carga sin el valor

typedef struct {
    uint64_t creador;
    uint64_t seq;
} n0_entidad_t;

typedef struct {
    uint8_t schema;
    uint8_t kind;
    uint64_t autor;
    uint8_t autor_tipo;
    uint64_t lamport;
    int64_t reloj_pared;  // solo para mostrar; NUNCA ordena
    uint16_t tipo_entidad;
    n0_entidad_t entidad;
    uint16_t campo;
    uint64_t version_campo;
    uint16_t n_valor;
    uint8_t valor[N0_MAX_VALOR];
} n0_op_t;

uint32_t n0_crc32(const uint8_t *d, size_t n);

// Devuelve los bytes escritos, o 0 si no cabe.
size_t n0_op_codificar(uint8_t *dst, size_t cap, const n0_op_t *op);

typedef enum {
    N0_DEC_OK = 0,
    N0_DEC_CORTO,     // faltan bytes: fin del registro válido
    N0_DEC_CRC,       // el CRC no cuadra: registro roto
    N0_DEC_SCHEMA,    // schema más nuevo del que entendemos
} n0_dec_t;

// Decodifica una operación. *consumido queda con los bytes leídos.
n0_dec_t n0_op_decodificar(const uint8_t *src, size_t n, n0_op_t *op, size_t *consumido);

// Pasada de recuperación: recorre validando y devuelve la longitud del prefijo
// VÁLIDO. Es lo que se ejecuta al arrancar para truncar un registro que se
// quedó a medias por un corte de corriente o por sacar la tarjeta.
size_t n0_op_recuperar(const uint8_t *log, size_t n, int *n_ops, int *no_interpretadas);

static inline bool n0_entidad_igual(n0_entidad_t a, n0_entidad_t b) {
    return a.creador == b.creador && a.seq == b.seq;
}

#endif
