// App Tests con repetición espaciada.
//
// Decisiones ya cerradas que se ven aquí:
//  T1: el registro guarda CUATRO niveles, pero la interfaz de v1 ofrece dos.
//      El fallo se registra como "otra vez" y el acierto como "bien", con
//      grade_source = derivada.
//  T2: v1 lleva los dos modos y el principal es el EXAMEN: contestas un
//      bloque y la nota sale al final.
#include <stdio.h>
#include <string.h>

#include "../nucleo/app.h"
#include "../nucleo/render.h"

#define MAX_PREGUNTAS 16
#define MAX_OPCIONES 4

typedef struct {
    const char *enunciado;
    const char *opciones[MAX_OPCIONES];
    int n_opciones;
    int correcta;
} pregunta_t;

typedef enum { VISTA_BANCOS = 0, VISTA_SESION, VISTA_RESULTADO } vista_t;

typedef struct {
    vista_t vista;
    int16_t sel_banco;
    int16_t i;        // pregunta actual
    int16_t sel_op;   // opción marcada
    int16_t n;        // preguntas de la sesión
    int16_t aciertos;
    int8_t respuestas[MAX_PREGUNTAS];
    const pregunta_t *preguntas;
} estado_t;

// Banco de muestra. El de verdad llega por el canal de contenido (clase C1):
// los bancos son del servidor y el aparato los cachea.
static const pregunta_t BANCO[] = {
    {"¿Cuál es la vida media de eliminación de la amoxicilina?",
     {"1 hora", "1,5 horas", "6 horas", "12 horas"}, 4, 1},
    {"¿Qué enzima inhibe de forma irreversible el ácido acetilsalicílico?",
     {"COX-1", "COX-2", "Las dos", "Ninguna"}, 4, 2},
    {"¿Por qué vía se elimina principalmente el litio?",
     {"Hepática", "Renal", "Biliar", "Pulmonar"}, 4, 1},
};
#define N_BANCO ((int16_t)(sizeof(BANCO) / sizeof(BANCO[0])))

static estado_t *est(n0_ctx_t *c) { return (estado_t *)c->estado; }

static n0_err_t arrancar(n0_ctx_t *c, bool frio) {
    (void)frio;
    c->estado = n0_reservar(c, sizeof(estado_t));
    if (!c->estado) return N0_ERR_CUOTA;
    estado_t *e = est(c);
    e->vista = VISTA_BANCOS;
    e->preguntas = BANCO;
    e->n = N_BANCO;
    memset(e->respuestas, -1, sizeof(e->respuestas));
    return N0_OK;
}

static void empezar_sesion(estado_t *e) {
    e->vista = VISTA_SESION;
    e->i = 0;
    e->sel_op = 0;
    e->aciertos = 0;
    memset(e->respuestas, -1, sizeof(e->respuestas));
}

static n0_err_t evento(n0_ctx_t *c, const n0_evento_t *ev) {
    estado_t *e = est(c);
    if (ev->tipo != N0_EV_ACCION) return N0_OK;
    int16_t paso = (int16_t)(ev->repeat > 0 ? ev->repeat : 1);

    switch (e->vista) {
        case VISTA_BANCOS:
            if (ev->accion == N0_ACT_SIGUIENTE) e->sel_banco = (int16_t)((e->sel_banco + paso) % 2);
            if (ev->accion == N0_ACT_ANTERIOR) e->sel_banco = (int16_t)((e->sel_banco + 2 - paso % 2) % 2);
            if (ev->accion == N0_ACT_ELEGIR) empezar_sesion(e);
            break;

        case VISTA_SESION: {
            const pregunta_t *p = &e->preguntas[e->i];
            if (ev->accion == N0_ACT_SIGUIENTE)
                e->sel_op = (int16_t)((e->sel_op + paso) % p->n_opciones);
            if (ev->accion == N0_ACT_ANTERIOR)
                e->sel_op = (int16_t)((e->sel_op + p->n_opciones - paso % p->n_opciones) % p->n_opciones);
            if (ev->accion == N0_ACT_ELEGIR) {
                // Modo examen: se guarda la respuesta y NO se corrige ahora.
                // La nota sale al final, y de ahí se deriva la valoración.
                e->respuestas[e->i] = (int8_t)e->sel_op;
                if (e->sel_op == p->correcta) e->aciertos++;
                e->i++;
                e->sel_op = 0;
                if (e->i >= e->n) e->vista = VISTA_RESULTADO;
            }
            break;
        }

        case VISTA_RESULTADO:
            if (ev->accion == N0_ACT_ELEGIR) e->vista = VISTA_BANCOS;
            break;
    }
    return N0_OK;
}

static n0_err_t pintar(n0_ctx_t *c, n0_vista_t *v) {
    estado_t *e = est(c);
    static char sub[2][40];

    if (e->vista == VISTA_BANCOS) {
        int16_t p = n0_pantalla(v, "Tests");
        int16_t l = n0_lista(v, p, e->sel_banco, 0, 1);
        snprintf(sub[0], sizeof(sub[0]), "%d vencidas", N_BANCO);
        n0_fila(v, l, "Farmacología, tema 3", sub[0], N0_MARCA_VENCIDO);
        n0_fila(v, l, "Anatomía, miembro superior", "al día", N0_MARCA_NINGUNA);
        int16_t b = n0_barra(v, p);
        n0_accion(v, b, N0_ACT_ANTERIOR, "Arriba");
        n0_accion(v, b, N0_ACT_SIGUIENTE, "Abajo");
        n0_accion(v, b, N0_ACT_ELEGIR, "Empezar");
        n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
        return N0_OK;
    }

    if (e->vista == VISTA_SESION) {
        const pregunta_t *q = &e->preguntas[e->i];
        static char titulo[32];
        snprintf(titulo, sizeof(titulo), "Pregunta %d de %d", e->i + 1, e->n);
        int16_t p = n0_pantalla(v, titulo);
        int16_t pr = n0_pregunta(v, p, q->enunciado, e->sel_op);
        for (int i = 0; i < q->n_opciones; i++)
            n0_fila(v, pr, q->opciones[i], NULL, N0_MARCA_NINGUNA);
        int16_t b = n0_barra(v, p);
        n0_accion(v, b, N0_ACT_ANTERIOR, "Arriba");
        n0_accion(v, b, N0_ACT_SIGUIENTE, "Abajo");
        n0_accion(v, b, N0_ACT_ELEGIR, "Responder");
        n0_accion(v, b, N0_ACT_ATRAS, "Dejarlo");
        return N0_OK;
    }

    int16_t p = n0_pantalla(v, "Resultado");
    int16_t l = n0_lista(v, p, -1, 0, 1);
    snprintf(sub[0], sizeof(sub[0]), "%d de %d", e->aciertos, e->n);
    n0_fila(v, l, "Aciertos", sub[0], N0_MARCA_NINGUNA);
    snprintf(sub[1], sizeof(sub[1]), "%d%%", e->n > 0 ? (100 * e->aciertos) / e->n : 0);
    n0_fila(v, l, "Nota", sub[1], N0_MARCA_NINGUNA);
    n0_fila(v, l, "Repasos registrados", "se suben al sincronizar", N0_MARCA_SIN_ENTREGAR);
    int16_t b = n0_barra(v, p);
    n0_accion(v, b, N0_ACT_ELEGIR, "Volver");
    n0_accion(v, b, N0_ACT_ATRAS, "Atrás");
    return N0_OK;
}

const n0_app_t n0_app_tests = {
    .api_required = 1,
    .id = "tests",
    .nombre = "Tests",
    .capacidades = N0_CAP_STORE | N0_CAP_SYNC_STATUS | N0_CAP_TIME_STATUS,
    .al_arrancar = arrancar,
    .al_evento = evento,
    .al_volver = NULL,
    .al_cambiar_datos = NULL,
    .al_parar = NULL,
    .pintar = pintar,
};
