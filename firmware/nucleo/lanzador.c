#include "lanzador.h"

#include <stdio.h>
#include <string.h>

// Todas las capacidades que este núcleo conoce. Una que no esté aquí se
// RECHAZA al cargar: ignorarla la concedería en silencio como "ninguna" y el
// fallo aparecería en el aparato en vez de en la publicación.
#define N0_CAPS_CONOCIDAS                                                            \
    (N0_CAP_STORE | N0_CAP_SYNC_STATUS | N0_CAP_SYNC_FORCE | N0_CAP_POWER_READ |     \
     N0_CAP_DEVICE_CONTROL | N0_CAP_RADIO | N0_CAP_UPDATE | N0_CAP_DIAG |            \
     N0_CAP_TIME_STATUS)

const char *n0_err_codigo(n0_err_t e) {
    switch (e) {
        case N0_OK: return "OK";
        case N0_ERR_ARG: return "APP-01";
        case N0_ERR_ESTADO: return "APP-02";
        case N0_ERR_CUOTA: return "STORE-01";
        case N0_ERR_NO_ENCONTRADO: return "STORE-02";
        case N0_ERR_SIN_CAP: return "APP-03";
        case N0_ERR_PLAZO: return "APP-04";
        case N0_ERR_ALMACEN: return "STORE-03";
        case N0_ERR_ESQUEMA: return "APP-05";
        case N0_ERR_INTERNO: return "SYS-01";
    }
    return "SYS-01";
}

const char *n0_carga_str(n0_carga_t c) {
    switch (c) {
        case N0_CARGA_OK: return "ok";
        case N0_CARGA_SIN_SITIO: return "no caben más apps";
        case N0_CARGA_API_FUERA_DE_RANGO: return "api_required fuera del rango del núcleo";
        case N0_CARGA_CAP_DESCONOCIDA: return "pide una capacidad que no existe";
        case N0_CARGA_SIN_PINTAR: return "no tiene función de pintar";
    }
    return "?";
}

void *n0_reservar(n0_ctx_t *c, size_t n) {
    n = (n + 7u) & ~(size_t)7u;  // alineado a 8
    if (c->arena_usada + n > c->arena_n) return NULL;
    void *p = (char *)c->arena + c->arena_usada;
    c->arena_usada += n;
    memset(p, 0, n);
    return p;
}

void n0_lanzador_init(n0_lanzador_t *l, const n0_servicios_t *sv, uint64_t (*ahora_us)(void)) {
    memset(l, 0, sizeof(*l));
    l->sv = sv;
    l->ahora_us = ahora_us;
    l->n_pila = 0;
}

n0_carga_t n0_cargar(n0_lanzador_t *l, const n0_app_t *app, void *arena, size_t arena_n) {
    if (l->n_apps >= N0_MAX_APPS) return N0_CARGA_SIN_SITIO;
    if (app->api_required < N0_API_FLOOR || app->api_required > N0_API_VERSION)
        return N0_CARGA_API_FUERA_DE_RANGO;
    if (app->capacidades & ~(uint32_t)N0_CAPS_CONOCIDAS) return N0_CARGA_CAP_DESCONOCIDA;
    if (app->pintar == NULL) return N0_CARGA_SIN_PINTAR;

    int i = l->n_apps++;
    l->apps[i].app = app;
    l->apps[i].fallos_seguidos = 0;
    l->apps[i].en_cuarentena = false;
    memset(l->apps[i].us_peor, 0, sizeof(l->apps[i].us_peor));

    n0_ctx_t *c = &l->ctx[i];
    memset(c, 0, sizeof(*c));
    c->app_id = app->id;
    c->capacidades = app->capacidades;
    c->sv = l->sv;
    c->arena = arena;
    c->arena_n = arena_n;
    c->arena_usada = 0;
    return N0_CARGA_OK;
}

static int buscar(const n0_lanzador_t *l, const char *id) {
    for (int i = 0; i < l->n_apps; i++)
        if (strcmp(l->apps[i].app->id, id) == 0) return i;
    return -1;
}

int n0_activa(const n0_lanzador_t *l) {
    return l->n_pila > 0 ? l->pila[l->n_pila - 1] : -1;
}

// anotar_fallo deja la app, cuenta el fallo y aplica la cuarentena. Libera
// además los cerrojos de vigilia pedidos en su nombre: los posee el sistema,
// así que un fallo de app no puede impedir dormir.
static void anotar_fallo(n0_lanzador_t *l, int idx, n0_err_t e, const char *gancho) {
    l->ultimo_err = e;
    l->ultima_app = l->apps[idx].app->id;
    l->ultimo_gancho = gancho;
    l->cerrojos_vigilia = 0;
    l->ctx[idx].arena_usada = 0;
    l->ctx[idx].estado = NULL;
    if (l->apps[idx].fallos_seguidos < 255) l->apps[idx].fallos_seguidos++;
    if (l->apps[idx].fallos_seguidos >= N0_CUARENTENA) l->apps[idx].en_cuarentena = true;
    if (l->n_pila > 0 && l->pila[l->n_pila - 1] == idx) l->n_pila--;
}

// El contador se pone a cero SOLO cuando la app termina una sesión limpia, no
// cada vez que un gancho devuelve N0_OK.
//
// Resetearlo en cada gancho bueno parecía razonable y estaba mal: una app que
// arranca bien y revienta en CADA pulsación volvía a cero en cada arranque, y
// no entraba en cuarentena nunca. "Dos fallos consecutivos" tiene que
// significar "sin una sesión buena en medio", o el umbral no muerde.
static void anotar_bien(n0_lanzador_t *l, int idx) {
    l->apps[idx].fallos_seguidos = 0;
}

// Cronometrar un gancho: se pide la hora antes, se llama, y `medido` cierra.
// Trata UN ERROR DEVUELTO Y UN PLAZO AGOTADO EXACTAMENTE IGUAL.
//
// El presupuesto es cooperativo: no interrumpe a nadie. Lo que contiene a una
// app que no vuelve es que su tarea sea matable y que exista el perro
// guardián; esto detecta a la que vuelve tarde.
//
// Va como función y no como macro con `({...})` a propósito: eso es una
// extensión de GNU, y el núcleo se compila tal cual para el aparato.
static uint64_t ahora(const n0_lanzador_t *l) {
    return l->ahora_us ? l->ahora_us() : 0;
}

static n0_err_t medido(n0_lanzador_t *l, int idx, int gancho, uint32_t presup_us,
                       n0_err_t e, uint64_t t0) {
    uint64_t dt = ahora(l) - t0;
    if (dt > l->apps[idx].us_peor[gancho]) l->apps[idx].us_peor[gancho] = (uint32_t)dt;
    if (e == N0_OK && presup_us > 0 && dt > presup_us) return N0_ERR_PLAZO;
    return e;
}

n0_err_t n0_abrir(n0_lanzador_t *l, const char *app_id, bool frio) {
    int i = buscar(l, app_id);
    if (i < 0) return N0_ERR_NO_ENCONTRADO;
    if (l->apps[i].en_cuarentena) return N0_ERR_ESTADO;
    if (l->n_pila >= N0_MAX_PILA) return N0_ERR_ESTADO;

    l->pila[l->n_pila++] = i;
    const n0_app_t *a = l->apps[i].app;
    if (!a->al_arrancar) return N0_OK;

    uint64_t t0 = ahora(l);
    n0_err_t e = medido(l, i, 0, N0_PRESUP_ARRANCAR_US, a->al_arrancar(&l->ctx[i], frio), t0);
    if (e != N0_OK) {
        anotar_fallo(l, i, e, "al_arrancar");
        return e;
    }
    return N0_OK;
}

n0_err_t n0_evento(n0_lanzador_t *l, const n0_evento_t *ev) {
    int i = n0_activa(l);
    if (i < 0) return N0_ERR_ESTADO;
    const n0_app_t *a = l->apps[i].app;

    // Atrás con una app que no maneja eventos es volver al lanzador.
    if (ev->tipo == N0_EV_ACCION &&
        (ev->accion == N0_ACT_ATRAS || ev->accion == N0_ACT_ATRAS_LARGO) && !a->al_evento) {
        return n0_cerrar(l);
    }
    if (!a->al_evento) return N0_OK;

    n0_err_t e;
    uint64_t t0 = ahora(l);
    if (ev->tipo == N0_EV_DATOS_CAMBIADOS && a->al_cambiar_datos) {
        e = medido(l, i, 3, N0_PRESUP_DATOS_US, a->al_cambiar_datos(&l->ctx[i], ev->arg), t0);
    } else {
        e = medido(l, i, 1, N0_PRESUP_EVENTO_US, a->al_evento(&l->ctx[i], ev), t0);
    }
    if (e != N0_OK) {
        anotar_fallo(l, i, e, ev->tipo == N0_EV_DATOS_CAMBIADOS ? "al_cambiar_datos" : "al_evento");
        return e;
    }
    return N0_OK;
}

n0_err_t n0_cerrar(n0_lanzador_t *l) {
    int i = n0_activa(l);
    if (i < 0) return N0_ERR_ESTADO;
    const n0_app_t *a = l->apps[i].app;
    n0_err_t e = N0_OK;
    if (a->al_parar) {
        uint64_t t0 = ahora(l);
        e = medido(l, i, 4, N0_PRESUP_PARAR_US, a->al_parar(&l->ctx[i]), t0);
    }
    // Se abandona pase lo que pase: cerrar descriptores y tirar la arena.
    l->ctx[i].arena_usada = 0;
    l->ctx[i].estado = NULL;
    l->cerrojos_vigilia = 0;
    if (l->n_pila > 0) l->n_pila--;
    if (e != N0_OK) {
        l->ultimo_err = e;
        l->ultima_app = l->apps[i].app->id;
        l->ultimo_gancho = "al_parar";
    } else {
        // Sesión terminada limpiamente: eso, y solo eso, borra la racha.
        anotar_bien(l, i);
    }
    // Al volver, la de debajo recibe al_volver.
    int j = n0_activa(l);
    if (j >= 0 && l->apps[j].app->al_volver) {
        uint64_t t1 = ahora(l);
        n0_err_t e2 = medido(l, j, 2, N0_PRESUP_VOLVER_US, l->apps[j].app->al_volver(&l->ctx[j]), t1);
        if (e2 != N0_OK) anotar_fallo(l, j, e2, "al_volver");
    }
    return e;
}

void n0_desenrollar(n0_lanzador_t *l) {
    // No pregunta y no puede fallar: el invariante de dormir siempre gana.
    while (l->n_pila > 0) {
        int i = l->pila[l->n_pila - 1];
        if (l->apps[i].app->al_parar) {
            uint64_t t0 = ahora(l);
            (void)medido(l, i, 4, N0_PRESUP_PARAR_US, l->apps[i].app->al_parar(&l->ctx[i]), t0);
        }
        l->ctx[i].arena_usada = 0;
        l->ctx[i].estado = NULL;
        l->n_pila--;
    }
    l->cerrojos_vigilia = 0;
}

// Pantalla de error del sistema. La construye el NÚCLEO, no la app culpable:
// una app que acaba de fallar no está en condiciones de pintar su propio
// error. Y cumple las mismas reglas que exige a las apps: dice qué pasó, qué
// hacer, y ofrece acciones.
static void vista_error(n0_vista_t *v, const char *codigo, const char *app, const char *gancho) {
    static char frase[128];
    snprintf(frase, sizeof(frase), "La app «%s» falló en %s.", app ? app : "?", gancho ? gancho : "?");
    n0_vista_init(v);
    int16_t p = n0_pantalla(v, "Algo ha fallado");
    n0_error(v, p, codigo, frase, "Vuelve al menú. Si se repite, la app queda desactivada.");
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ATRAS, "Al menú");
}

void n0_pintar(n0_lanzador_t *l, n0_lienzo_t *lz, const n0_maqueta_t *m) {
    static n0_vista_t v;
    int i = n0_activa(l);

    if (i >= 0) {
        n0_vista_init(&v);
        uint64_t t0 = ahora(l);
        n0_err_t e = medido(l, i, 5, N0_PRESUP_PINTAR_US, l->apps[i].app->pintar(&l->ctx[i], &v), t0);

        if (e == N0_OK) {
            n0_vista_err_t ve = n0_render(lz, &v, m);
            if (ve == N0_VISTA_OK) return;
            // Un árbol inválido es un fallo de la app, no una pantalla a medias.
            e = N0_ERR_ESQUEMA;
        }
        anotar_fallo(l, i, e, "pintar");
    }

    // Nunca se devuelve un lienzo sin pintar.
    vista_error(&v, n0_err_codigo(l->ultimo_err ? l->ultimo_err : N0_ERR_INTERNO),
                l->ultima_app, l->ultimo_gancho);
    (void)n0_render(lz, &v, m);
}
