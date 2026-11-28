// fusion — el motor de fusión del registro de operaciones, lado del aparato.
//
// Las MISMAS reglas que internal/merge del servidor, §4 del documento de
// sincronización. Si los dos lados no convergen al mismo estado, la
// sincronización está rota de una forma que no se nota hasta meses después.
//
// Sin asignación dinámica, y con una decisión de memoria que importa: un campo
// guarda sus METADATOS de fusión y un (desplazamiento, longitud) dentro de un
// ALMACÉN DE VALORES que pone quien llama. Guardar el valor en línea, con el
// tope de 4 KB que admite una operación, daba un estado de 3,6 MB — imposible
// en un ESP32-S3, que tiene 512 KB de SRAM. Con el almacén de valores baja a
// unas decenas de KB y el tope lo elige quien llama.
#ifndef NITR0S_FUSION_H
#define NITR0S_FUSION_H

#include "opfmt.h"

#define N0_MAX_ENTIDADES 96
#define N0_MAX_CAMPOS 8
#define N0_MAX_CONFLICTOS 16
#define N0_CONF_VALOR 64  // del perdedor se guarda lo que quepa: es para mostrar

typedef enum { N0_DUENO_APARATO = 1, N0_DUENO_SERVIDOR = 2, N0_DUENO_COMPARTIDO = 3 } n0_dueno_t;

// Metadatos de la operación GANADORA de un campo. Todo lo que la fusión
// necesita comparar, sin el valor.
typedef struct {
    uint16_t campo;
    bool usado;
    uint8_t kind;
    uint8_t autor_tipo;
    uint64_t autor;
    uint64_t lamport;
    uint64_t version;
    uint16_t n_valor;
    uint32_t off_valor;  // dentro del almacén de valores
} n0_campo_t;

typedef struct {
    n0_entidad_t id;
    uint16_t tipo;
    bool creada;
    uint64_t crear_autor;
    uint64_t crear_lamport;
    n0_campo_t campos[N0_MAX_CAMPOS];
    int n_campos;
} n0_ent_t;

typedef struct {
    n0_entidad_t entidad;
    uint16_t campo;
    uint8_t autor_perdedor;
    uint16_t n_perdedor;
    uint8_t perdedor[N0_CONF_VALOR];
    const char *motivo;
} n0_conflicto_t;

typedef struct {
    n0_ent_t ents[N0_MAX_ENTIDADES];
    int n;
    n0_conflicto_t conflictos[N0_MAX_CONFLICTOS];
    int n_conflictos;
    bool desbordado;  // no cupo todo: el estado NO es de fiar y hay que decirlo

    uint8_t *valores;  // almacén de valores, lo pone quien llama
    size_t val_cap;
    size_t val_n;

    n0_dueno_t (*dueno)(uint16_t tipo_entidad);
} n0_fusion_t;

// `valores` y `val_cap` los pone quien llama: en el aparato, un bloque
// estático; en el simulador, lo que haga falta.
void n0_fusion_init(n0_fusion_t *f, n0_dueno_t (*dueno)(uint16_t),
                    uint8_t *valores, size_t val_cap);

void n0_fusion_aplicar(n0_fusion_t *f, const n0_op_t *op);
size_t n0_fusion_aplicar_log(n0_fusion_t *f, const uint8_t *log, size_t n);

const n0_ent_t *n0_fusion_entidad(const n0_fusion_t *f, n0_entidad_t id);
const n0_campo_t *n0_ent_campo(const n0_ent_t *e, uint16_t campo);

// El valor de un campo, dentro del almacén. NO está terminado en cero: lleva su
// longitud, porque un valor puede contener cualquier byte.
const uint8_t *n0_ent_valor(const n0_fusion_t *f, const n0_ent_t *e, uint16_t campo, uint16_t *n);
bool n0_ent_borrada(const n0_fusion_t *f, const n0_ent_t *e);

void n0_fusion_ordenar(n0_fusion_t *f);

#endif
