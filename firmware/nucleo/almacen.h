// almacen — la API de datos que ven las apps, sobre el registro de operaciones.
//
// Una escritura que devuelve N0_OK está EN EL REGISTRO, con marco y CRC, y
// sobrevive a un corte de corriente. No está necesariamente sincronizada, y la
// app no tiene forma de preguntarlo porque no debe: eso lo cuenta la consola
// con sus dos estados de entrega.
//
// Las apps escriben aquí, el almacén añade al registro, y `sinc` lo drena
// según su calendario. Exponer la sincronización a las apps invertiría la
// dependencia y destruiría en silencio el funcionamiento sin red.
#ifndef NITR0S_ALMACEN_H
#define NITR0S_ALMACEN_H

#include "app.h"
#include "fusion.h"

typedef struct {
    uint8_t *log;      // registro de salida, solo se añade
    size_t cap;
    size_t n;          // bytes usados
    n0_fusion_t fus;   // estado fusionado, derivado del registro

    uint64_t device_id;
    uint64_t lamport;      // monótono, se persiste ANTES de usarse
    uint64_t creator_seq;  // reparte los identificadores que acuña este aparato
    bool lleno;            // el registro no admite más: cuota agotada
} n0_almacen_t;

// `valores` y `val_cap` son el almacén de valores del fusionador: lo pone
// quien llama, igual que el registro.
void n0_almacen_init(n0_almacen_t *a, uint8_t *log, size_t cap, uint64_t device_id,
                     n0_dueno_t (*dueno)(uint16_t), uint8_t *valores, size_t val_cap);

// Carga un registro existente: pasada de recuperación y fusión. Devuelve los
// bytes válidos, que pueden ser menos que n si el final estaba roto.
size_t n0_almacen_cargar(n0_almacen_t *a, const uint8_t *bytes, size_t n);

// Acuña una entidad nueva. El par (creador, seq) es único por construcción:
// no necesita entropía, que es justo lo que no hay cuando esp_random() corre
// sin radio.
n0_entidad_t n0_almacen_crear(n0_almacen_t *a, uint16_t tipo);

// Escribe un campo. La versión se calcula desde el registro, que es la verdad.
n0_err_t n0_almacen_fijar(n0_almacen_t *a, n0_entidad_t e, uint16_t tipo,
                          uint16_t campo, const void *valor, uint16_t n);
n0_err_t n0_almacen_borrar(n0_almacen_t *a, n0_entidad_t e, uint16_t tipo);

// Fusiona operaciones que vienen del servidor. No se emiten: ya son hechos de
// otro autor.
void n0_almacen_recibir(n0_almacen_t *a, const uint8_t *log, size_t n);

// La tabla de servicios que el lanzador entrega a las apps.
extern const n0_servicios_t N0_SERVICIOS_ALMACEN;

// El almacén activo. En el aparato hay uno; se deja explícito en vez de
// escondido para que se vea que es estado global del sistema, no de la app.
void n0_almacen_activar(n0_almacen_t *a);

#endif
