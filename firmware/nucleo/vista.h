// vista — el árbol de vista, conjunto CERRADO de nodos.
//
// Especificado en diseno/api-del-nucleo.md §5. Una app devuelve una
// DESCRIPCIÓN, nunca dibujo: de la comparación de dos árboles sale la
// diferencia de rectángulos sucios, y así no se ejecuta código de app en el
// camino crítico del refresco.
//
// Ningún nodo lleva píxeles, fuente ni color. Si los llevara, la fase visual
// quedaría decidida por omisión aquí, que es lo que la corrección 8 prohíbe.
#ifndef NITR0S_VISTA_H
#define NITR0S_VISTA_H

#include <stdbool.h>
#include <stdint.h>

#define N0_MAX_NODOS 256  // tope del árbol: acota la memoria de render (§8)

typedef enum {
    N0_NODO_PANTALLA = 0,
    N0_NODO_LISTA,
    N0_NODO_FILA,
    N0_NODO_KV,
    N0_NODO_TEXTO,
    N0_NODO_PREGUNTA,
    N0_NODO_VACIO,
    N0_NODO_OCUPADO,
    N0_NODO_ERROR,
    N0_NODO_INSIGNIA,
    N0_NODO_BARRA,   // barra de acciones; su forma es decisión visual
    N0_NODO_ACCION,
} n0_nodo_tipo_t;

// Estado de una fila. Semántico, no visual: "qué significa", no "cómo se pinta".
typedef enum {
    N0_MARCA_NINGUNA = 0,
    N0_MARCA_HECHO,
    N0_MARCA_PENDIENTE,
    N0_MARCA_VENCIDO,
    N0_MARCA_SIN_ENTREGAR,
} n0_marca_t;

// Acciones semánticas. Una app nunca ve un código de botón: el número y la
// posición de los botones están sin confirmar (Q7), y usar códigos crudos
// congelaría un hecho de hardware sin verificar dentro de la ABI.
typedef enum {
    N0_ACT_SIGUIENTE = 0,
    N0_ACT_ANTERIOR,
    N0_ACT_ELEGIR,
    N0_ACT_ATRAS,
    N0_ACT_MENU,
    N0_ACT_1,
    N0_ACT_2,
    N0_ACT_3,
    N0_ACT_4,
    N0_ACT_ELEGIR_LARGO,
    N0_ACT_ATRAS_LARGO,
    N0_ACT__MAX,
} n0_accion_t;

typedef struct {
    uint8_t tipo;         // n0_nodo_tipo_t
    uint8_t marca;        // n0_marca_t
    uint8_t accion;       // n0_accion_t, en N0_NODO_ACCION
    int16_t hijo;         // índice del primer hijo, -1 si no hay
    int16_t hermano;      // índice del siguiente hermano, -1 si no hay
    int16_t sel;          // índice seleccionado, en lista y pregunta
    int16_t pagina;       // página actual, en lista y texto
    int16_t paginas;      // páginas totales
    const char *a;        // texto principal / etiqueta / código de error
    const char *b;        // texto secundario / valor / frase humana
    const char *c;        // QUÉ HACER: obligatorio en vacío y en error
} n0_nodo_t;

typedef struct {
    n0_nodo_t nodos[N0_MAX_NODOS];
    int16_t n;
    bool desbordado;  // se intentó pasar del tope: el árbol no vale
} n0_vista_t;

void n0_vista_init(n0_vista_t *v);

// Cada constructor devuelve el índice del nodo, o -1 si se desbordó.
int16_t n0_pantalla(n0_vista_t *v, const char *titulo);
int16_t n0_lista(n0_vista_t *v, int16_t padre, int16_t sel, int16_t pagina, int16_t paginas);
int16_t n0_fila(n0_vista_t *v, int16_t padre, const char *texto, const char *secundario, n0_marca_t m);
int16_t n0_kv(n0_vista_t *v, int16_t padre, const char *etiqueta, const char *valor);
int16_t n0_texto(n0_vista_t *v, int16_t padre, const char *contenido, int16_t pagina, int16_t paginas);
int16_t n0_pregunta(n0_vista_t *v, int16_t padre, const char *enunciado, int16_t sel);
int16_t n0_vacio(n0_vista_t *v, int16_t padre, const char *que_falta, const char *que_hacer);
int16_t n0_ocupado(n0_vista_t *v, int16_t padre, const char *haciendo, int16_t paso, int16_t total);
int16_t n0_error(n0_vista_t *v, int16_t padre, const char *codigo, const char *frase, const char *que_hacer);
int16_t n0_insignia(n0_vista_t *v, int16_t padre, n0_marca_t m);
int16_t n0_barra(n0_vista_t *v, int16_t padre);
int16_t n0_accion(n0_vista_t *v, int16_t barra, n0_accion_t a, const char *etiqueta);

// Resultado de validar un árbol. El núcleo lo exige ANTES de pintar, que es lo
// que convierte el cuidado de la sección 13 del brief en algo comprobable en
// vez de en algo que cada sesión tiene que recordar.
typedef enum {
    N0_VISTA_OK = 0,
    N0_VISTA_DESBORDADA,
    N0_VISTA_SIN_PANTALLA,
    N0_VISTA_SIN_ACCIONES,   // ninguna pantalla puede presentar cero acciones
    N0_VISTA_VACIO_SIN_SALIDA,  // un vacío tiene que decir QUÉ HACER
    N0_VISTA_ERROR_SIN_SALIDA,  // y un error también
    N0_VISTA_SIN_VUELTA,        // siempre hay vuelta atrás
} n0_vista_err_t;

n0_vista_err_t n0_vista_validar(const n0_vista_t *v);
const char *n0_vista_err_str(n0_vista_err_t e);

#endif
