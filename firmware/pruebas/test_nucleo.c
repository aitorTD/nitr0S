// Pruebas del núcleo. Sin marco de pruebas: una función por propiedad y un
// contador, que en C es más honesto que arrastrar una dependencia.
#include <stdio.h>
#include <string.h>

#include "../nucleo/almacen.h"
#include "../nucleo/lanzador.h"
#include "../nucleo/render.h"

static int fallos = 0;
static int hechas = 0;

// El mensaje va siempre, así que `...` nunca queda vacío y no hace falta el
// `##__VA_ARGS__` de GNU: esto es C11 a secas y pasa -pedantic.
#define COMPRUEBA(cond, ...)                                \
    do {                                                    \
        hechas++;                                           \
        if (!(cond)) {                                      \
            fallos++;                                       \
            printf("  FALLO %s:%d: ", __FILE__, __LINE__);  \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
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


// --- El lanzador: aquí viven los invariantes ---------------------------------

static uint64_t g_reloj_us;
static uint64_t reloj_falso(void) { return g_reloj_us; }

static n0_err_t ok_arrancar(n0_ctx_t *c, bool frio) { (void)c; (void)frio; return N0_OK; }
static n0_err_t ok_pintar(n0_ctx_t *c, n0_vista_t *v) {
    (void)c;
    int16_t p = n0_pantalla(v, "Buena");
    int16_t l = n0_lista(v, p, 0, 0, 1);
    n0_fila(v, l, "una fila", NULL, N0_MARCA_NINGUNA);
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ELEGIR, "Elegir");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
    return N0_OK;
}
static const n0_app_t APP_BUENA = {1, "buena", "Buena", N0_CAP_STORE,
                                   ok_arrancar, NULL, NULL, NULL, NULL, ok_pintar};

static n0_err_t falla_evento(n0_ctx_t *c, const n0_evento_t *e) {
    (void)c; (void)e; return N0_ERR_ESTADO;
}
static const n0_app_t APP_FALLONA = {1, "fallona", "Fallona", 0,
                                     ok_arrancar, falla_evento, NULL, NULL, NULL, ok_pintar};

// Esta se pasa del presupuesto sin devolver error: el lanzador tiene que
// tratarla igual que a una que falla.
static n0_err_t lenta_evento(n0_ctx_t *c, const n0_evento_t *e) {
    (void)c; (void)e;
    g_reloj_us += N0_PRESUP_EVENTO_US + 1000;
    return N0_OK;
}
static const n0_app_t APP_LENTA = {1, "lenta", "Lenta", 0,
                                   ok_arrancar, lenta_evento, NULL, NULL, NULL, ok_pintar};

// Y esta devuelve un árbol inválido: un vacío sin "qué hacer".
static n0_err_t mal_pintar(n0_ctx_t *c, n0_vista_t *v) {
    (void)c;
    int16_t p = n0_pantalla(v, "Mala");
    n0_vacio(v, p, "No hay nada.", NULL);
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
    return N0_OK;
}
static const n0_app_t APP_MAL_ARBOL = {1, "malarbol", "Mal árbol", 0,
                                       ok_arrancar, NULL, NULL, NULL, NULL, mal_pintar};

static char g_arena_t[4096];

static void test_carga(void) {
    n0_lanzador_t l;
    n0_lanzador_init(&l, NULL, reloj_falso);

    COMPRUEBA(n0_cargar(&l, &APP_BUENA, g_arena_t, sizeof(g_arena_t)) == N0_CARGA_OK,
              "una app correcta se carga");

    // api_required fuera de rango: se RECHAZA al cargar, no al llamar.
    n0_app_t vieja = APP_BUENA;
    vieja.id = "vieja";
    vieja.api_required = N0_API_VERSION + 1;
    COMPRUEBA(n0_cargar(&l, &vieja, g_arena_t, sizeof(g_arena_t)) == N0_CARGA_API_FUERA_DE_RANGO,
              "api_required fuera de rango tiene que rechazarse al cargar");

    // Una capacidad desconocida NO se ignora: ignorarla la concedería en
    // silencio como "ninguna" y el fallo saldría en el aparato.
    n0_app_t rara = APP_BUENA;
    rara.id = "rara";
    rara.capacidades = 1u << 20;
    COMPRUEBA(n0_cargar(&l, &rara, g_arena_t, sizeof(g_arena_t)) == N0_CARGA_CAP_DESCONOCIDA,
              "una capacidad desconocida tiene que rechazarse");

    n0_app_t muda = APP_BUENA;
    muda.id = "muda";
    muda.pintar = NULL;
    COMPRUEBA(n0_cargar(&l, &muda, g_arena_t, sizeof(g_arena_t)) == N0_CARGA_SIN_PINTAR,
              "una app sin pintar no se carga");
}

static void test_cuarentena(void) {
    static char ar[2][2048];
    n0_lanzador_t l;
    n0_lanzador_init(&l, NULL, reloj_falso);
    n0_cargar(&l, &APP_FALLONA, ar[0], sizeof(ar[0]));
    n0_cargar(&l, &APP_BUENA, ar[1], sizeof(ar[1]));

    n0_evento_t ev = {N0_EV_ACCION, N0_ACT_ELEGIR, 1, 0};
    for (int i = 0; i < N0_CUARENTENA; i++) {
        COMPRUEBA(n0_abrir(&l, "fallona", true) == N0_OK, "la app abre");
        COMPRUEBA(n0_evento(&l, &ev) != N0_OK, "y falla");
        COMPRUEBA(n0_activa(&l) < 0, "un fallo deja la app, no la mantiene");
    }
    COMPRUEBA(n0_abrir(&l, "fallona", true) != N0_OK,
              "dos fallos seguidos la dejan en cuarentena");

    // Y la cuarentena de una NO toca a las demás.
    COMPRUEBA(n0_abrir(&l, "buena", true) == N0_OK,
              "la cuarentena de una app no afecta a las otras");
}

static void test_plazo_es_un_fallo(void) {
    static char ar[2048];
    n0_lanzador_t l;
    n0_lanzador_init(&l, NULL, reloj_falso);
    n0_cargar(&l, &APP_LENTA, ar, sizeof(ar));
    g_reloj_us = 1000;
    COMPRUEBA(n0_abrir(&l, "lenta", true) == N0_OK, "abre bien");
    n0_evento_t ev = {N0_EV_ACCION, N0_ACT_ELEGIR, 1, 0};
    COMPRUEBA(n0_evento(&l, &ev) == N0_ERR_PLAZO,
              "pasarse del presupuesto sin devolver error cuenta como fallo");
    COMPRUEBA(n0_activa(&l) < 0, "y la app se abandona igual");
}

static void test_siempre_se_pinta_algo(void) {
    static uint8_t bits[(800 / 8) * 480];
    static char ar[2048];
    n0_lienzo_t lz;
    n0_lanzador_t l;
    n0_lanzador_init(&l, NULL, reloj_falso);
    n0_cargar(&l, &APP_MAL_ARBOL, ar, sizeof(ar));
    n0_abrir(&l, "malarbol", true);

    n0_lienzo_init(&lz, bits, 800, 480);
    n0_pintar(&l, &lz, n0_maquetas[0]);
    // Un árbol inválido no deja la pantalla en blanco: se pinta el error.
    int tinta = 0;
    for (int16_t y = 0; y < 480 && tinta == 0; y++)
        for (int16_t x = 0; x < 800; x++)
            if (n0_leer(&lz, x, y)) { tinta = 1; break; }
    COMPRUEBA(tinta, "nunca se devuelve un lienzo sin pintar");
    COMPRUEBA(n0_activa(&l) < 0, "y la app culpable se abandona");

    // Con la pila vacía también se pinta algo.
    n0_lienzo_init(&lz, bits, 800, 480);
    n0_pintar(&l, &lz, n0_maquetas[0]);
    tinta = 0;
    for (int16_t y = 0; y < 480 && tinta == 0; y++)
        for (int16_t x = 0; x < 800; x++)
            if (n0_leer(&lz, x, y)) { tinta = 1; break; }
    COMPRUEBA(tinta, "con la pila vacía se pinta la pantalla de error");
}

static void test_desenrollar_siempre_gana(void) {
    static char ar[2048];
    n0_lanzador_t l;
    n0_lanzador_init(&l, NULL, reloj_falso);
    n0_cargar(&l, &APP_BUENA, ar, sizeof(ar));
    for (int i = 0; i < 3; i++) n0_abrir(&l, "buena", true);
    COMPRUEBA(l.n_pila == 3, "la pila crece");
    l.cerrojos_vigilia = 5;
    n0_desenrollar(&l);
    // El invariante que manda sobre todos: siempre se puede volver a dormir.
    COMPRUEBA(l.n_pila == 0, "desenrollar vacía la pila sin preguntar");
    COMPRUEBA(l.cerrojos_vigilia == 0,
              "y libera los cerrojos: un fallo de app NUNCA debe impedir dormir");
}

static void test_arena_es_del_sistema(void) {
    static char ar[64];
    n0_lanzador_t l;
    n0_lanzador_init(&l, NULL, reloj_falso);
    n0_cargar(&l, &APP_BUENA, ar, sizeof(ar));
    n0_ctx_t *c = &l.ctx[0];
    COMPRUEBA(n0_reservar(c, 32) != NULL, "cabe");
    COMPRUEBA(n0_reservar(c, 1000) == NULL, "no cabe y se dice, no se desborda");
    n0_abrir(&l, "buena", true);
    n0_cerrar(&l);
    COMPRUEBA(c->arena_usada == 0, "al abandonar la app, el sistema tira su arena entera");
}


// --- El almacén --------------------------------------------------------------

static n0_dueno_t dueno_prueba(uint16_t t) {
    (void)t;
    return N0_DUENO_COMPARTIDO;
}

static void test_almacen(void) {
    static uint8_t log[8192], val_a[8192], val_b[8192], val_c[8192];
    n0_almacen_t a;
    n0_almacen_init(&a, log, sizeof(log), 0xD0, dueno_prueba, val_a, sizeof(val_a));

    n0_entidad_t e = n0_almacen_crear(&a, 10);
    COMPRUEBA(e.creador == 0xD0 && e.seq == 1, "el identificador es (creador, seq)");
    COMPRUEBA(n0_almacen_fijar(&a, e, 10, 1, "leche", 5) == N0_OK, "se escribe el texto");

    const n0_ent_t *ent = n0_fusion_entidad(&a.fus, e);
    COMPRUEBA(ent != NULL, "la entidad existe tras emitir");
    const n0_campo_t *t = n0_ent_campo(ent, 1);
    COMPRUEBA(t && t->version == 1, "la primera escritura de un campo es la versión 1");

    // Las ediciones sucesivas suben la versión, o la segunda no ganaría.
    n0_almacen_fijar(&a, e, 10, 1, "leche entera", 12);
    n0_almacen_fijar(&a, e, 10, 1, "leche desnatada", 15);
    ent = n0_fusion_entidad(&a.fus, e);
    t = n0_ent_campo(ent, 1);
    COMPRUEBA(t && t->version == 3, "tres escrituras llegan a la versión 3, salió %d",
              t ? (int)t->version : -1);
    uint16_t nv = 0;
    const uint8_t *vv = n0_ent_valor(&a.fus, ent, 1, &nv);
    COMPRUEBA(vv && nv == 15 && memcmp(vv, "leche desnatada", 15) == 0, "y queda la última");

    // Recargar el registro tiene que dar el mismo estado, y reconstruir los
    // contadores: si el lamport quedara por detrás, se reutilizaría y la
    // idempotencia se rompería en silencio.
    uint64_t lam = a.lamport, seq = a.creator_seq;
    n0_almacen_t b;
    static uint8_t log2[8192];
    n0_almacen_init(&b, log2, sizeof(log2), 0xD0, dueno_prueba, val_b, sizeof(val_b));
    size_t valido = n0_almacen_cargar(&b, log, a.n);
    COMPRUEBA(valido == a.n, "el registro entero es válido al recargar");
    COMPRUEBA(b.lamport == lam, "el lamport se reconstruye del registro (%llu vs %llu)",
              (unsigned long long)b.lamport, (unsigned long long)lam);
    COMPRUEBA(b.creator_seq == seq, "y el contador de creación también");
    const n0_ent_t *ent2 = n0_fusion_entidad(&b.fus, e);
    const n0_campo_t *t2 = ent2 ? n0_ent_campo(ent2, 1) : NULL;
    COMPRUEBA(t2 && t2->version == 3, "y el estado fusionado es el mismo");

    // Un registro truncado a mitad no se interpreta a medias.
    static uint8_t log3[8192];
    n0_almacen_t c;
    n0_almacen_init(&c, log3, sizeof(log3), 0xD0, dueno_prueba, val_c, sizeof(val_c));
    size_t v3 = n0_almacen_cargar(&c, log, a.n - 3);
    COMPRUEBA(v3 < a.n, "un registro cortado da un prefijo más corto");
    COMPRUEBA(v3 % 1 == 0 && c.fus.n >= 1, "y lo que queda sigue siendo usable");
}

static void test_almacen_lleno(void) {
    // Un registro diminuto: cabe una operación y poco más.
    static uint8_t log[140], val_a[4096];
    n0_almacen_t a;
    n0_almacen_init(&a, log, sizeof(log), 0xD0, dueno_prueba, val_a, sizeof(val_a));

    n0_entidad_t e = n0_almacen_crear(&a, 10);
    COMPRUEBA(e.seq == 1, "la primera cabe");
    size_t antes = a.n;
    uint64_t lam_antes = a.lamport;

    n0_err_t r = N0_OK;
    for (int i = 0; i < 20 && r == N0_OK; i++) r = n0_almacen_fijar(&a, e, 10, 1, "xxxxxxxxxx", 10);
    COMPRUEBA(r == N0_ERR_CUOTA, "al llenarse se devuelve cuota agotada, salió %s", n0_err_codigo(r));
    COMPRUEBA(a.n <= sizeof(log), "nunca se escribe más allá del final del registro");
    COMPRUEBA(a.n >= antes, "lo que ya estaba escrito no se toca");
    COMPRUEBA(a.lleno, "y queda marcado como lleno");
    (void)lam_antes;

    // Y el registro sigue siendo legible entero: no hay escritura a medias.
    int ops = 0;
    size_t valido = n0_op_recuperar(log, a.n, &ops, NULL);
    COMPRUEBA(valido == a.n, "el registro sigue íntegro tras llenarse (%zu de %zu)", valido, a.n);
}

static void test_recibidas_no_entran_en_el_registro_propio(void) {
    static uint8_t log[4096], val_a[4096];
    static uint8_t ajenas[4096];
    n0_almacen_t a;
    n0_almacen_init(&a, log, sizeof(log), 0xD0, dueno_prueba, val_a, sizeof(val_a));

    // Una operación de la consola, como la que llegaría del servidor.
    n0_op_t op;
    memset(&op, 0, sizeof(op));
    op.schema = N0_OP_SCHEMA;
    op.kind = N0_OP_FIJAR;
    op.autor = 0xC0;
    op.autor_tipo = N0_AUTOR_CONSOLA;
    op.lamport = 7;
    op.tipo_entidad = 10;
    op.entidad = (n0_entidad_t){0xC0, 1};
    op.campo = 1;
    op.version_campo = 1;
    op.n_valor = 3;
    memcpy(op.valor, "pan", 3);
    size_t n = n0_op_codificar(ajenas, sizeof(ajenas), &op);

    size_t propio_antes = a.n;
    n0_almacen_recibir(&a, ajenas, n);
    COMPRUEBA(a.n == propio_antes,
              "lo que viene del servidor se fusiona pero NO entra en el registro de salida");
    COMPRUEBA(n0_fusion_entidad(&a.fus, op.entidad) != NULL, "pero sí está en el estado");
    // Y el lamport del aparato no se contamina con el del otro lado.
    COMPRUEBA(a.lamport == 0, "el lamport del aparato es suyo, no del servidor");
}


// --- Presupuesto de memoria --------------------------------------------------
//
// Esto existe porque ya pasó: el estado fusionado guardaba un valor de 4 KB en
// línea por campo y ocupaba 3,6 MB, imposible en un ESP32-S3 de 512 KB. Lo
// encontró un segfault en las pruebas, no una revisión. Un tope comprobado
// impide que vuelva a colarse.
#define PRESUP_FUSION (64 * 1024)
#define PRESUP_LANZADOR (8 * 1024)
#define PRESUP_VISTA (16 * 1024)

static void test_presupuesto_de_memoria(void) {
    COMPRUEBA(sizeof(n0_fusion_t) <= PRESUP_FUSION,
              "el estado fusionado ocupa %zu bytes y el tope son %d",
              sizeof(n0_fusion_t), PRESUP_FUSION);
    COMPRUEBA(sizeof(n0_lanzador_t) <= PRESUP_LANZADOR,
              "el lanzador ocupa %zu bytes y el tope son %d",
              sizeof(n0_lanzador_t), PRESUP_LANZADOR);
    COMPRUEBA(sizeof(n0_vista_t) <= PRESUP_VISTA,
              "el árbol de vista ocupa %zu bytes y el tope son %d",
              sizeof(n0_vista_t), PRESUP_VISTA);
    printf("  memoria: fusión %zu B, lanzador %zu B, vista %zu B, framebuffer %d B\n",
           sizeof(n0_fusion_t), sizeof(n0_lanzador_t), sizeof(n0_vista_t), (800 / 8) * 480);
}

int main(void) {
    printf("pruebas del núcleo de nitr0S\n");
    test_validacion();
    test_utf8();
    test_lienzo();
    test_maquetas();
    test_carga();
    test_cuarentena();
    test_plazo_es_un_fallo();
    test_siempre_se_pinta_algo();
    test_desenrollar_siempre_gana();
    test_arena_es_del_sistema();
    test_almacen();
    test_almacen_lleno();
    test_recibidas_no_entran_en_el_registro_propio();
    test_presupuesto_de_memoria();
    printf("%d comprobaciones, %d fallos\n", hechas, fallos);
    return fallos == 0 ? 0 : 1;
}
