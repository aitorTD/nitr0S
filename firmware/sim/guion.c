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

#include "../nucleo/almacen.h"
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
static uint8_t g_log[64 * 1024];
static uint8_t g_valores[32 * 1024];
static n0_almacen_t g_almacen;

// El aparato de v1 solo tiene entidades compartidas; el esquema de verdad lo
// publicará el manifiesto.
static n0_dueno_t dueno(uint16_t tipo) {
    (void)tipo;
    return N0_DUENO_COMPARTIDO;
}

// La tabla de servicios se COMPONE: los datos los da el almacén de verdad, y
// la batería y el diagnóstico siguen siendo de mentira porque no hay hardware.
// Que esto sea una tabla de punteros y no herencia es lo que lo hace posible.
static n0_servicios_t componer(void) {
    n0_servicios_t sv = N0_SERVICIOS_ALMACEN;
    sv.bateria = ALMACEN_FALSO.bateria;
    sv.diag = ALMACEN_FALSO.diag;
    sv.sync_forzar = ALMACEN_FALSO.sync_forzar;
    return sv;
}

// Carga el registro que produjo el servidor, si está. Son los MISMOS bytes que
// la prueba cruzada: lo que se pinta en la pantalla sale de operaciones que
// fusionó el servidor en Go.
static size_t cargar_del_servidor(const char *ruta, uint8_t *dst, size_t cap) {
    FILE *f = fopen(ruta, "rb");
    if (!f) return 0;
    size_t n = fread(dst, 1, cap, f);
    fclose(f);
    return n;
}

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

    // Almacén de verdad: registro de operaciones y fusión, el mismo código que
    // pasa la prueba cruzada contra el servidor.
    n0_almacen_init(&g_almacen, g_log, sizeof(g_log), 0xD0, dueno, g_valores, sizeof(g_valores));
    n0_almacen_activar(&g_almacen);

    static uint8_t del_servidor[64 * 1024];
    size_t ns = cargar_del_servidor("pruebas/datos/registro.bin", del_servidor, sizeof(del_servidor));
    if (ns > 0) {
        n0_almacen_recibir(&g_almacen, del_servidor, ns);
        printf("recibidas %zu bytes de operaciones del servidor\n", ns);
    } else {
        // Sin el registro del servidor, el aparato crea las suyas: es el caso
        // de un aparato que todavía no ha sincronizado nunca.
        const char *suyas[] = {"Leche entera", "Pan de centeno", "Pilas AAA", "Café molido"};
        for (size_t i = 0; i < sizeof(suyas) / sizeof(suyas[0]); i++) {
            n0_entidad_t e = n0_almacen_crear(&g_almacen, 10);
            n0_almacen_fijar(&g_almacen, e, 10, 1, suyas[i], (uint16_t)strlen(suyas[i]));
        }
        printf("sin registro del servidor: el aparato ha creado %d ítems propios\n", 4);
    }
    n0_fusion_ordenar(&g_almacen.fus);

    static n0_servicios_t sv;
    sv = componer();
    n0_lanzador_init(&l, &sv, ahora_us);
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
    printf("registro de salida del aparato: %zu bytes, %d entidades fusionadas\n",
           g_almacen.n, g_almacen.fus.n);
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
