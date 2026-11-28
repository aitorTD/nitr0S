// Pruebas del núcleo. Sin marco de pruebas: una función por propiedad y un
// contador, que en C es más honesto que arrastrar una dependencia.
#include <stdio.h>
#include <string.h>

#include "../nucleo/render.h"

static int fallos = 0;
static int hechas = 0;

#define COMPRUEBA(cond, msg, ...)                                     \
    do {                                                              \
        hechas++;                                                     \
        if (!(cond)) {                                                \
            fallos++;                                                 \
            printf("  FALLO %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        }                                                             \
    } while (0)

extern const n0_fuente_t n0_fuente_cuerpo;
extern const n0_maqueta_t *const n0_maquetas[];
extern const int n0_n_maquetas;

// --- El árbol de vista exige lo que la sección 13 del brief pide -------------

static void barra_minima(n0_vista_t *v, int16_t p) {
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ELEGIR, "Elegir");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
}

static void test_validacion(void) {
    n0_vista_t v;

    // Un estado vacío SIN "qué hacer" no es un estado vacío diseñado.
    n0_vista_init(&v);
    int16_t p = n0_pantalla(&v, "Compra");
    n0_vacio(&v, p, "No hay nada.", NULL);
    barra_minima(&v, p);
    COMPRUEBA(n0_vista_validar(&v) == N0_VISTA_VACIO_SIN_SALIDA,
              "un vacío sin 'qué hacer' tiene que rechazarse");

    // Con el "qué hacer", pasa.
    n0_vista_init(&v);
    p = n0_pantalla(&v, "Compra");
    n0_vacio(&v, p, "No hay nada.", "Añade algo desde la consola.");
    barra_minima(&v, p);
    COMPRUEBA(n0_vista_validar(&v) == N0_VISTA_OK, "con 'qué hacer' debe valer");

    // Un error sin salida, igual.
    n0_vista_init(&v);
    p = n0_pantalla(&v, "Sincronizar");
    n0_error(&v, p, "NET-01", "No se pudo conectar.", NULL);
    barra_minima(&v, p);
    COMPRUEBA(n0_vista_validar(&v) == N0_VISTA_ERROR_SIN_SALIDA,
              "un error sin 'qué puede hacer el usuario' tiene que rechazarse");

    // Ninguna pantalla puede presentar cero acciones posibles.
    n0_vista_init(&v);
    p = n0_pantalla(&v, "Compra");
    n0_lista(&v, p, 0, 0, 1);
    COMPRUEBA(n0_vista_validar(&v) == N0_VISTA_SIN_ACCIONES,
              "una pantalla sin acciones tiene que rechazarse");

    // Siempre hay vuelta atrás.
    n0_vista_init(&v);
    p = n0_pantalla(&v, "Compra");
    int16_t b = n0_barra(&v, p);
    n0_accion(&v, b, N0_ACT_ELEGIR, "Elegir");
    COMPRUEBA(n0_vista_validar(&v) == N0_VISTA_SIN_VUELTA,
              "una pantalla sin vuelta atrás tiene que rechazarse");

    // El árbol tiene tope, y pasarse se detecta en vez de corromper memoria.
    n0_vista_init(&v);
    p = n0_pantalla(&v, "Compra");
    int16_t l = n0_lista(&v, p, 0, 0, 1);
    for (int i = 0; i < N0_MAX_NODOS + 10; i++) n0_fila(&v, l, "x", NULL, N0_MARCA_NINGUNA);
    COMPRUEBA(v.desbordado, "pasarse del tope tiene que marcarse");
    COMPRUEBA(v.n <= N0_MAX_NODOS, "nunca se escriben más de N0_MAX_NODOS nodos, hay %d", v.n);
    COMPRUEBA(n0_vista_validar(&v) == N0_VISTA_DESBORDADA, "un árbol desbordado no vale");
}

// --- UTF-8: el español lo necesita ------------------------------------------

static void test_utf8(void) {
    uint32_t cp;
    COMPRUEBA(n0_utf8("a", &cp) == 1 && cp == 'a', "ASCII");
    COMPRUEBA(n0_utf8("ñ", &cp) == 2 && cp == 0xF1, "ñ son dos bytes, salió U+%04X", cp);
    COMPRUEBA(n0_utf8("¿", &cp) == 2 && cp == 0xBF, "¿");
    COMPRUEBA(n0_utf8("€", &cp) == 3 && cp == 0x20AC, "tres bytes");
    COMPRUEBA(n0_utf8("", &cp) == 0, "fin de cadena");
    // Un byte inválido no puede tumbar una pantalla.
    const char roto[] = {(char)0xFF, 'a', 0};
    COMPRUEBA(n0_utf8(roto, &cp) == 1 && cp == 0xFFFD, "byte inválido da el sustituto");

    // Y los acentos miden, es decir: hay glifo, no se caen en silencio.
    COMPRUEBA(n0_texto_ancho(&n0_fuente_cuerpo, "Plátanos") > n0_texto_ancho(&n0_fuente_cuerpo, "Pltanos"),
              "la tilde tiene que ocupar");
    COMPRUEBA(n0_texto_ancho(&n0_fuente_cuerpo, "¿Añoño?") > 0, "cobertura del español");
}

// --- El lienzo ---------------------------------------------------------------

static void test_lienzo(void) {
    static uint8_t bits[(64 / 8) * 32];
    n0_lienzo_t l;
    n0_lienzo_init(&l, bits, 64, 32);

    COMPRUEBA(n0_leer(&l, 0, 0) == N0_PAPEL, "empieza en papel");
    n0_punto(&l, 3, 4, N0_TINTA);
    COMPRUEBA(n0_leer(&l, 3, 4) == N0_TINTA, "un punto se pinta");
    COMPRUEBA(n0_leer(&l, 4, 4) == N0_PAPEL, "y no mancha al vecino");

    // Fuera del lienzo no escribe ni revienta.
    n0_punto(&l, -1, -1, N0_TINTA);
    n0_punto(&l, 1000, 1000, N0_TINTA);
    COMPRUEBA(n0_leer(&l, 1000, 1000) == 0, "fuera del lienzo se ignora");

    // Invertir dos veces es la identidad: es el acuse de recibo, y tiene que
    // poder deshacerse sin dejar rastro.
    n0_rect_t r = {0, 0, 16, 8};
    n0_rect_lleno(&l, (n0_rect_t){2, 2, 5, 3}, N0_TINTA);
    uint8_t antes[(64 / 8) * 32];
    memcpy(antes, bits, sizeof(bits));
    n0_invertir(&l, r);
    COMPRUEBA(memcmp(antes, bits, sizeof(bits)) != 0, "invertir cambia algo");
    n0_invertir(&l, r);
    COMPRUEBA(memcmp(antes, bits, sizeof(bits)) == 0, "invertir dos veces es la identidad");

    // El recorte impide que una fila pinte fuera de su sitio.
    n0_limpiar(&l);
    n0_recorte(&l, (n0_rect_t){10, 10, 5, 5});
    n0_rect_lleno(&l, (n0_rect_t){0, 0, 64, 32}, N0_TINTA);
    n0_sin_recorte(&l);
    COMPRUEBA(n0_leer(&l, 12, 12) == N0_TINTA, "dentro del recorte sí");
    COMPRUEBA(n0_leer(&l, 0, 0) == N0_PAPEL, "fuera del recorte no");
    COMPRUEBA(n0_leer(&l, 15, 15) == N0_PAPEL, "el borde del recorte es exclusivo");
}

// --- Las maquetas -------------------------------------------------------------

static void test_maquetas(void) {
    static uint8_t bits[(800 / 8) * 480];
    n0_lienzo_t l;
    n0_vista_t v;

    for (int i = 0; i < n0_n_maquetas; i++) {
        const n0_maqueta_t *m = n0_maquetas[i];
        int16_t fpp = n0_filas_por_pagina(m, 480);
        COMPRUEBA(fpp > 0, "la maqueta %s no deja ni una fila", m->nombre);

        // Lo que la maqueta dice que cabe tiene que caber de verdad: el cuerpo
        // no puede desbordar por debajo de la barra de acciones.
        int16_t y_cab = m->barra_arriba ? (int16_t)(m->margen_y + m->alto_barra) : m->margen_y;
        int16_t y_cuerpo = (int16_t)(y_cab + m->alto_cabecera);
        int16_t fin = m->barra_arriba ? (int16_t)(480 - m->margen_y)
                                      : (int16_t)(480 - m->margen_y - m->alto_barra);
        COMPRUEBA(y_cuerpo + fpp * m->alto_fila <= fin,
                  "la maqueta %s dice %d filas pero se sale (%d > %d)",
                  m->nombre, fpp, y_cuerpo + fpp * m->alto_fila, fin);

        // Y una pantalla llena se pinta sin que el renderizador la rechace.
        n0_vista_init(&v);
        int16_t p = n0_pantalla(&v, "Compra");
        int16_t li = n0_lista(&v, p, 1, 0, 3);
        for (int k = 0; k < fpp; k++) n0_fila(&v, li, "Aceite de oliva virgen extra", "el de 5 L", N0_MARCA_HECHO);
        barra_minima(&v, p);
        n0_lienzo_init(&l, bits, 800, 480);
        COMPRUEBA(n0_render(&l, &v, m) == N0_VISTA_OK, "la maqueta %s no pinta", m->nombre);
    }
}

int main(void) {
    printf("pruebas del núcleo de nitr0S\n");
    test_validacion();
    test_utf8();
    test_lienzo();
    test_maquetas();
    printf("%d comprobaciones, %d fallos\n", hechas, fallos);
    return fallos == 0 ? 0 : 1;
}
