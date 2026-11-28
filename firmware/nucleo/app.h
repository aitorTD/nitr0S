// app — la costura entre el núcleo y las apps.
//
// Es la cabecera que B9 del brief exigía ANTES de la primera app, y la
// especifica diseno/api-del-nucleo.md. Seis funciones y dos tipos opacos.
//
// No hay ninguna función de dibujo, ninguna de red y ninguna de fichero, y no
// es una omisión: son las no-capacidades de §5.1 expresadas en la cabecera.
#ifndef NITR0S_APP_H
#define NITR0S_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vista.h"

#define N0_API_VERSION 1  // la que ofrece este núcleo
#define N0_API_FLOOR 1    // la más vieja que sigue cargando

typedef enum {
    N0_OK = 0,
    N0_ERR_ARG,
    N0_ERR_ESTADO,
    N0_ERR_CUOTA,
    N0_ERR_NO_ENCONTRADO,
    N0_ERR_SIN_CAP,    // capacidad no concedida: en el aparato no debería ocurrir
    N0_ERR_PLAZO,      // el gancho agotó su presupuesto de reloj
    N0_ERR_ALMACEN,
    N0_ERR_ESQUEMA,
    N0_ERR_INTERNO,
} n0_err_t;

const char *n0_err_codigo(n0_err_t e);  // "APP-03", para la pantalla de error

// --- Capacidades -------------------------------------------------------------
// Lista CERRADA. Lo que no está, no existe (manifiesto §5).
typedef enum {
    N0_CAP_STORE = 1u << 0,
    N0_CAP_SYNC_STATUS = 1u << 1,
    N0_CAP_SYNC_FORCE = 1u << 2,
    N0_CAP_POWER_READ = 1u << 3,
    N0_CAP_DEVICE_CONTROL = 1u << 4,
    N0_CAP_RADIO = 1u << 5,
    N0_CAP_UPDATE = 1u << 6,
    N0_CAP_DIAG = 1u << 7,
    N0_CAP_TIME_STATUS = 1u << 8,
} n0_cap_t;

// --- Eventos -----------------------------------------------------------------
typedef enum {
    N0_EV_ACCION = 0,       // una acción semántica
    N0_EV_DATOS_CAMBIADOS,  // un conjunto de datos suyo cambió
    N0_EV_BATERIA,
    N0_EV_RELOJ,
    N0_EV_SYNC,
} n0_ev_tipo_t;

typedef struct {
    uint8_t tipo;    // n0_ev_tipo_t
    uint8_t accion;  // n0_accion_t
    uint8_t repeat;  // pulsaciones agrupadas: nunca se descartan, se agrupan
    uint32_t arg;
} n0_evento_t;

// --- Servicios ---------------------------------------------------------------
// Cada puntero está presente SOLO si su capacidad fue concedida. Uno no
// concedido llega NULO y se comprueba al cargar, no al llamarlo.
typedef struct n0_ctx n0_ctx_t;

typedef enum { N0_SYNC_NUNCA = 0, N0_SYNC_AL_DIA, N0_SYNC_PENDIENTE, N0_SYNC_FALLANDO } n0_sync_estado_t;
typedef enum { N0_RELOJ_FIABLE = 0, N0_RELOJ_DUDOSO } n0_reloj_estado_t;

// La identidad de una entidad es el par (creador, seq) de 16 bytes. La app lo
// trata como opaco: lo recibe al iterar y lo devuelve al escribir.
typedef struct {
    uint64_t creador;
    uint64_t seq;
} n0_id_t;

typedef struct {
    n0_err_t (*lista_abrir)(n0_ctx_t *, uint32_t ds, void **it);
    // OJO: el `texto` que devuelve apunta a memoria DEL SISTEMA que se
    // reutiliza en la llamada siguiente. Si la app lo va a guardar, tiene que
    // COPIARLO a su arena. Guardar el puntero y seguir iterando hace que todas
    // las filas acaben enseñando el último texto leído, y es un fallo que no
    // se ve hasta que se mira la pantalla.
    n0_err_t (*lista_siguiente)(n0_ctx_t *, void *it, const char **texto, n0_id_t *id, uint8_t *marca);
    n0_err_t (*lista_cerrar)(n0_ctx_t *, void *it);
    n0_err_t (*marcar)(n0_ctx_t *, uint32_t ds, n0_id_t id, bool hecho);
    n0_err_t (*sync_estado)(n0_ctx_t *, n0_sync_estado_t *out);
    n0_err_t (*sync_forzar)(n0_ctx_t *);
    n0_err_t (*bateria)(n0_ctx_t *, int *pct, int *mv);
    n0_err_t (*reloj_estado)(n0_ctx_t *, n0_reloj_estado_t *out);
    n0_err_t (*diag)(n0_ctx_t *, const char **ultimo_fallo, uint32_t *reinicios);
} n0_servicios_t;

struct n0_ctx {
    const char *app_id;
    uint32_t capacidades;
    const n0_servicios_t *sv;
    void *estado;   // estado privado de la app, en su arena
    void *arena;    // la posee el SISTEMA y la libera entera
    size_t arena_n;
    size_t arena_usada;
};

// Reserva en la arena de la app. El sistema la tira entera al abandonarla, que
// es lo que hace que matar una app sea recuperable en vez de una fuga.
void *n0_reservar(n0_ctx_t *c, size_t n);

typedef struct {
    uint16_t api_required;
    const char *id;
    const char *nombre;
    uint32_t capacidades;  // lo que el manifiesto declara

    n0_err_t (*al_arrancar)(n0_ctx_t *, bool frio);
    n0_err_t (*al_evento)(n0_ctx_t *, const n0_evento_t *);
    n0_err_t (*al_volver)(n0_ctx_t *);
    n0_err_t (*al_cambiar_datos)(n0_ctx_t *, uint32_t dataset_id);
    n0_err_t (*al_parar)(n0_ctx_t *);
    n0_err_t (*pintar)(n0_ctx_t *, n0_vista_t *);
} n0_app_t;

#endif
