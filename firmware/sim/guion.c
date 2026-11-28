// Ejecuta el lanzador con las tres apps de v1 y un guion de pulsaciones, y
// escribe una tira de pantallas. Es el primer trozo del aparato funcionando de
// extremo a extremo: núcleo, lanzador, apps y renderizador, todo el código que
// acabará dentro.
// El simulador es código de anfitrión, así que puede pedir POSIX; el núcleo
// se queda en C11 estricto porque se compila tal cual para el aparato.
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../nucleo/lanzador.h"
#include "pbm.h"

#define ANCHO 800
#define ALTO 480

extern const n0_app_t n0_app_listas;
extern const n0_app_t n0_app_tests;
extern const n0_app_t n0_app_estado;
extern const n0_servicios_t ALMACEN_FALSO;
extern const n0_maqueta_t n0_maqueta_densa;

void almacen_reset(void);
void almacen_poner(uint32_t id, const char *texto, uint8_t marca);
void n0_listas_fijar_fpp(n0_ctx_t *c, int16_t fpp);

static uint64_t ahora_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000);
}

static uint8_t g_bits[((ANCHO + 7) / 8) * ALTO];
static char g_arena[3][4096];

typedef struct {
    const char *app;      // abrir esta app, o NULL para seguir
    n0_accion_t accion;
    uint8_t repeat;
    int cerrar;
    const char *nombre;   // nombre del fotograma
} paso_t;

int guion_ejecutar(const char *dir) {
    n0_lanzador_t l;
    n0_lienzo_t lz;
    char ruta[512];

    almacen_reset();
    almacen_poner(1, "Leche entera", N0_MARCA_NINGUNA);
    almacen_poner(2, "Pan de centeno", N0_MARCA_NINGUNA);
    almacen_poner(3, "Pilas AAA", N0_MARCA_NINGUNA);
    almacen_poner(4, "Bolsas de basura", N0_MARCA_NINGUNA);
    almacen_poner(5, "Café molido", N0_MARCA_NINGUNA);
    almacen_poner(6, "Champú", N0_MARCA_NINGUNA);

    n0_lanzador_init(&l, &ALMACEN_FALSO, ahora_us);
    const n0_app_t *apps[3] = {&n0_app_listas, &n0_app_tests, &n0_app_estado};
    for (int i = 0; i < 3; i++) {
        n0_carga_t c = n0_cargar(&l, apps[i], g_arena[i], sizeof(g_arena[i]));
        if (c != N0_CARGA_OK) {
            fprintf(stderr, "no se pudo cargar %s: %s\n", apps[i]->id, n0_carga_str(c));
            return 1;
        }
    }

    const paso_t GUION[] = {
        {"listas", 0, 0, 0, "01-listas-abre"},
        {NULL, N0_ACT_SIGUIENTE, 2, 0, "02-listas-baja-dos"},
        {NULL, N0_ACT_ELEGIR, 0, 0, "03-listas-marca"},
        {NULL, N0_ACT_SIGUIENTE, 1, 0, "04-listas-baja"},
        {NULL, N0_ACT_ELEGIR, 0, 0, "05-listas-marca-otra"},
        {NULL, 0, 0, 1, "06-listas-cierra"},
        {"tests", 0, 0, 0, "07-tests-bancos"},
        {NULL, N0_ACT_ELEGIR, 0, 0, "08-tests-pregunta-1"},
        {NULL, N0_ACT_SIGUIENTE, 1, 0, "09-tests-elige-b"},
        {NULL, N0_ACT_ELEGIR, 0, 0, "10-tests-pregunta-2"},
        {NULL, N0_ACT_SIGUIENTE, 2, 0, "11-tests-elige-c"},
        {NULL, N0_ACT_ELEGIR, 0, 0, "12-tests-pregunta-3"},
        {NULL, N0_ACT_ELEGIR, 0, 0, "13-tests-resultado"},
        {NULL, 0, 0, 1, "14-tests-cierra"},
        {"estado", 0, 0, 0, "15-estado"},
        {NULL, N0_ACT_ELEGIR, 0, 0, "16-estado-sincroniza"},
    };
    const int N = (int)(sizeof(GUION) / sizeof(GUION[0]));

    int escritas = 0;
    for (int i = 0; i < N; i++) {
        const paso_t *p = &GUION[i];
        if (p->app) {
            n0_err_t e = n0_abrir(&l, p->app, true);
            if (e != N0_OK) fprintf(stderr, "  abrir %s dio %s\n", p->app, n0_err_codigo(e));
            // El shell le dice a Listas cuántas filas caben: la maqueta no es
            // cosa de la app.
            int a = n0_activa(&l);
            if (a >= 0 && strcmp(p->app, "listas") == 0)
                n0_listas_fijar_fpp(&l.ctx[a], n0_filas_por_pagina(&n0_maqueta_densa, ALTO));
        } else if (p->cerrar) {
            n0_cerrar(&l);
        } else {
            n0_evento_t ev = {N0_EV_ACCION, (uint8_t)p->accion, p->repeat, 0};
            n0_err_t e = n0_evento(&l, &ev);
            if (e != N0_OK) fprintf(stderr, "  evento dio %s\n", n0_err_codigo(e));
        }

        if (n0_activa(&l) < 0 && p->cerrar) continue;  // pila vacía: nada que pintar

        n0_lienzo_init(&lz, g_bits, ANCHO, ALTO);
        n0_pintar(&l, &lz, &n0_maqueta_densa);
        snprintf(ruta, sizeof(ruta), "%s/%s.pbm", dir, p->nombre);
        if (n0_pbm_escribir(&lz, ruta) == 0) escritas++;
    }

    printf("%d fotogramas escritos en %s\n", escritas, dir);
    printf("peor tiempo por gancho (us): ");
    for (int i = 0; i < l.n_apps; i++) {
        uint32_t peor = 0;
        for (int g = 0; g < 6; g++)
            if (l.apps[i].us_peor[g] > peor) peor = l.apps[i].us_peor[g];
        printf("%s=%u ", l.apps[i].app->id, peor);
    }
    printf("\n");

    // Y el invariante que manda: desenrollar no pregunta y deja la pila vacía.
    n0_desenrollar(&l);
    if (l.n_pila != 0 || l.cerrojos_vigilia != 0) {
        fprintf(stderr, "desenrollar no dejó el sistema listo para dormir\n");
        return 1;
    }
    return 0;
}
