// lanzador — el dueño de la pila de pantallas y de los invariantes.
//
// Trata UN ERROR DEVUELTO Y UN PLAZO AGOTADO EXACTAMENTE IGUAL: deja la app,
// pinta una pantalla legible con el código y la app culpable, escribe el
// registro y el sistema SIGUE.
//
// Y el invariante que manda sobre todo: un fallo de app NUNCA debe impedir
// dormir. Por eso los cerrojos de vigilia son del sistema, no de la app.
#ifndef NITR0S_LANZADOR_H
#define NITR0S_LANZADOR_H

#include "app.h"
#include "render.h"

#define N0_MAX_APPS 8
#define N0_MAX_PILA 8
#define N0_CUARENTENA 2  // dos fallos consecutivos: umbral único del proyecto

// Presupuestos de reloj por gancho, en microsegundos. Son CIFRAS DE DISEÑO, no
// medidas: se instrumentan desde el primer día y se ajustan con datos.
#define N0_PRESUP_ARRANCAR_US 50000
#define N0_PRESUP_EVENTO_US 50000
#define N0_PRESUP_VOLVER_US 20000
#define N0_PRESUP_DATOS_US 50000
#define N0_PRESUP_PARAR_US 20000
#define N0_PRESUP_PINTAR_US 10000

typedef struct {
    const n0_app_t *app;
    uint8_t fallos_seguidos;
    bool en_cuarentena;
    uint32_t us_peor[6];  // peor tiempo visto por gancho, para el diagnóstico
} n0_ranura_t;

typedef struct {
    n0_ranura_t apps[N0_MAX_APPS];
    int n_apps;

    int pila[N0_MAX_PILA];  // índices de app
    int n_pila;

    n0_ctx_t ctx[N0_MAX_APPS];

    // Último fallo, para la pantalla de error y para el diagnóstico.
    n0_err_t ultimo_err;
    const char *ultima_app;
    const char *ultimo_gancho;

    uint32_t cerrojos_vigilia;  // los posee el SISTEMA, nunca la app
    const n0_servicios_t *sv;
    uint64_t (*ahora_us)(void);
} n0_lanzador_t;

typedef enum {
    N0_CARGA_OK = 0,
    N0_CARGA_SIN_SITIO,
    N0_CARGA_API_FUERA_DE_RANGO,
    N0_CARGA_CAP_DESCONOCIDA,
    N0_CARGA_SIN_PINTAR,
} n0_carga_t;

const char *n0_carga_str(n0_carga_t c);

void n0_lanzador_init(n0_lanzador_t *l, const n0_servicios_t *sv, uint64_t (*ahora_us)(void));

// Carga una app. Comprueba api_required contra el rango y que no pida
// capacidades que no existen: se RECHAZA al cargar, no al llamar.
n0_carga_t n0_cargar(n0_lanzador_t *l, const n0_app_t *app, void *arena, size_t arena_n);

n0_err_t n0_abrir(n0_lanzador_t *l, const char *app_id, bool frio);
n0_err_t n0_evento(n0_lanzador_t *l, const n0_evento_t *ev);
n0_err_t n0_cerrar(n0_lanzador_t *l);  // desenrolla una pantalla

// Desenrolla la pila ENTERA sin preguntar: batería baja, app que se porta mal,
// volver a dormir. No pregunta y no puede fallar.
void n0_desenrollar(n0_lanzador_t *l);

// Pinta la app de arriba de la pila, o la pantalla de error si no hay nada que
// pintar. Nunca devuelve un lienzo sin pintar.
void n0_pintar(n0_lanzador_t *l, n0_lienzo_t *lz, const n0_maqueta_t *m);

int n0_activa(const n0_lanzador_t *l);  // índice de app activa, -1 si ninguna

#endif
