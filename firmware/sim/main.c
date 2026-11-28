// Simulador de escritorio de nitr0S.
//
// Ejecuta el MISMO núcleo que acabará en el aparato —el árbol de vista, el
// renderizador y las fuentes— y lo saca a ficheros PBM de 800x480 y 1 bit,
// que es la resolución y la profundidad reales del panel.
//
// Eso es lo que hace que una propuesta visual revisada aquí se parezca a lo
// que se verá, en vez de ser una maqueta que luego no cuadra (brief §12.3).
#include <stdio.h>
#include <string.h>

#include "../nucleo/render.h"
#include "pbm.h"

#define ANCHO 800
#define ALTO 480

extern const n0_maqueta_t *const n0_maquetas[];
extern const int n0_n_maquetas;

static uint8_t g_bits[((ANCHO + 7) / 8) * ALTO];

// --- Pantallas de muestra ----------------------------------------------------
//
// El contenido es de la app de Listas y de la de Tests. Las apps de verdad
// todavía no existen: esto construye el MISMO árbol de vista que construirían
// ellas, que es lo que hace falta para juzgar una maqueta.

static void pantalla_compra(n0_vista_t *v, int16_t sel, int16_t pag, int16_t pags) {
    n0_vista_init(v);
    int16_t p = n0_pantalla(v, "Compra");
    int16_t l = n0_lista(v, p, sel, pag, pags);
    n0_fila(v, l, "Leche entera", NULL, N0_MARCA_HECHO);
    n0_fila(v, l, "Pan de centeno", NULL, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Pilas AAA", NULL, N0_MARCA_HECHO);
    n0_fila(v, l, "Bolsas de basura", NULL, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Café molido", "2 paquetes", N0_MARCA_NINGUNA);
    n0_fila(v, l, "Champú", NULL, N0_MARCA_SIN_ENTREGAR);
    n0_fila(v, l, "Plátanos", NULL, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Queso curado", NULL, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Papel de aluminio", NULL, N0_MARCA_NINGUNA);
    n0_fila(v, l, "Aceite de oliva", "el de 5 L", N0_MARCA_NINGUNA);
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ANTERIOR, "Arriba");
    n0_accion(v, b, N0_ACT_SIGUIENTE, "Abajo");
    n0_accion(v, b, N0_ACT_ELEGIR, "Marcar");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
}

static void pantalla_tests(n0_vista_t *v) {
    n0_vista_init(v);
    int16_t p = n0_pantalla(v, "Tests");
    int16_t l = n0_lista(v, p, 1, 0, 1);
    n0_fila(v, l, "Farmacología, tema 3", "12 vencidas", N0_MARCA_VENCIDO);
    n0_fila(v, l, "Anatomía, miembro superior", "3 vencidas", N0_MARCA_VENCIDO);
    n0_fila(v, l, "Bioquímica, ciclo de Krebs", "al día", N0_MARCA_NINGUNA);
    n0_fila(v, l, "Fisiología, sistema renal", "nuevas: 20", N0_MARCA_PENDIENTE);
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ANTERIOR, "Arriba");
    n0_accion(v, b, N0_ACT_SIGUIENTE, "Abajo");
    n0_accion(v, b, N0_ACT_ELEGIR, "Empezar");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
}

static void pantalla_vacia(n0_vista_t *v) {
    n0_vista_init(v);
    int16_t p = n0_pantalla(v, "Compra");
    n0_vacio(v, p, "La lista está vacía.",
             "Añade cosas desde la consola, o pulsa Añadir.");
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ELEGIR, "Añadir");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
}

static void pantalla_error(n0_vista_t *v) {
    n0_vista_init(v);
    int16_t p = n0_pantalla(v, "Sincronizar");
    n0_error(v, p, "NET-01", "El servidor no acepta este aparato.",
             "Vuelve a aprovisionarlo con provision.json en la tarjeta.");
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ELEGIR, "Reintentar");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
}

static void pantalla_sin_reloj(n0_vista_t *v) {
    n0_vista_init(v);
    int16_t p = n0_pantalla(v, "Tests");
    n0_error(v, p, "TIME-02", "El reloj no es de fiar desde el último apagón.",
             "Los repasos se guardan en orden, pero sin fecha hasta sincronizar.");
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ELEGIR, "Repasar igual");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
}

typedef struct {
    const char *nombre;
    void (*construir)(n0_vista_t *);
} muestra_t;

static void m_compra(n0_vista_t *v) { pantalla_compra(v, 2, 0, 2); }

static const muestra_t MUESTRAS[] = {
    {"compra", m_compra},
    {"tests", pantalla_tests},
    {"vacia", pantalla_vacia},
    {"error", pantalla_error},
    {"sin-reloj", pantalla_sin_reloj},
};
static const int N_MUESTRAS = (int)(sizeof(MUESTRAS) / sizeof(MUESTRAS[0]));

int guion_ejecutar(const char *dir);
int sincronizar(const char *ruta_provision, const char *host, const char *puerto);

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : ".";
    if (argc > 2 && strcmp(argv[2], "-guion") == 0) return guion_ejecutar(dir);
    if (argc > 4 && strcmp(argv[2], "-sinc") == 0) return sincronizar(argv[3], argv[4], argc > 5 ? argv[5] : "8080");
    n0_lienzo_t l;
    n0_vista_t v;
    char ruta[512];
    int fallos = 0;

    for (int mi = 0; mi < n0_n_maquetas; mi++) {
        const n0_maqueta_t *m = n0_maquetas[mi];
        printf("maqueta %-8s  %2d filas por página  %s\n", m->nombre,
               n0_filas_por_pagina(m, ALTO), m->descripcion);
        for (int si = 0; si < N_MUESTRAS; si++) {
            MUESTRAS[si].construir(&v);
            n0_lienzo_init(&l, g_bits, ANCHO, ALTO);
            n0_vista_err_t e = n0_render(&l, &v, m);
            if (e != N0_VISTA_OK) {
                fprintf(stderr, "  %s/%s RECHAZADA: %s\n", m->nombre, MUESTRAS[si].nombre,
                        n0_vista_err_str(e));
                fallos++;
                continue;
            }
            snprintf(ruta, sizeof(ruta), "%s/%s-%s.pbm", dir, m->nombre, MUESTRAS[si].nombre);
            if (n0_pbm_escribir(&l, ruta) != 0) {
                fprintf(stderr, "  no se pudo escribir %s\n", ruta);
                fallos++;
            }
        }
    }
    printf("%d pantallas escritas en %s\n", n0_n_maquetas * N_MUESTRAS - fallos, dir);
    return fallos == 0 ? 0 : 1;
}
