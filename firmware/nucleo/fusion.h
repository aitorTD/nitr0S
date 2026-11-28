// fusion — el motor de fusión del registro de operaciones, lado del aparato.
//
// Las MISMAS reglas que internal/merge del servidor, §4 del documento de
// sincronización. Si los dos lados no convergen al mismo estado, la
// sincronización está rota de una forma que no se nota hasta meses después.
//
// Sin asignación dinámica: arrays de tamaño fijo. En el aparato no hay malloc
// que valga en el camino de arranque.
#ifndef NITR0S_FUSION_H
#define NITR0S_FUSION_H

#include "opfmt.h"

#define N0_MAX_ENTIDADES 96
#define N0_MAX_CAMPOS 8
#define N0_MAX_CONFLICTOS 32

typedef enum { N0_DUENO_APARATO = 1, N0_DUENO_SERVIDOR = 2, N0_DUENO_COMPARTIDO = 3 } n0_dueno_t;

typedef struct {
    uint16_t campo;
    bool usado;
    n0_op_t op;  // la operación GANADORA, no solo su valor: así la
                 // compactación puede reemitirla exacta
} n0_campo_t;

typedef struct {
    n0_entidad_t id;
    uint16_t tipo;
    bool creada;
    n0_op_t op_crear;
    n0_campo_t campos[N0_MAX_CAMPOS];
    int n_campos;
} n0_ent_t;

typedef struct {
    n0_entidad_t entidad;
    uint16_t campo;
    uint8_t autor_perdedor;
    uint16_t n_perdedor;
    uint8_t perdedor[64];
    const char *motivo;
} n0_conflicto_t;

typedef struct {
    n0_ent_t ents[N0_MAX_ENTIDADES];
    int n;
    n0_conflicto_t conflictos[N0_MAX_CONFLICTOS];
    int n_conflictos;
    bool desbordado;  // no cupo todo: el estado NO es de fiar
    n0_dueno_t (*dueno)(uint16_t tipo_entidad);
} n0_fusion_t;

void n0_fusion_init(n0_fusion_t *f, n0_dueno_t (*dueno)(uint16_t));
void n0_fusion_aplicar(n0_fusion_t *f, const n0_op_t *op);
size_t n0_fusion_aplicar_log(n0_fusion_t *f, const uint8_t *log, size_t n);

const n0_ent_t *n0_fusion_entidad(const n0_fusion_t *f, n0_entidad_t id);
bool n0_ent_borrada(const n0_ent_t *e);
const n0_op_t *n0_ent_campo(const n0_ent_t *e, uint16_t campo);

// Ordena las entidades por (creador, seq). El estado tiene que poder volcarse
// de forma determinista para poder compararlo con el del otro lado.
void n0_fusion_ordenar(n0_fusion_t *f);

#endif
