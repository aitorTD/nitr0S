#include "vista.h"

#include <string.h>

void n0_vista_init(n0_vista_t *v) {
    v->n = 0;
    v->desbordado = false;
}

static int16_t nuevo(n0_vista_t *v, int16_t padre, n0_nodo_tipo_t t) {
    if (v->n >= N0_MAX_NODOS) {
        v->desbordado = true;
        return -1;
    }
    int16_t i = v->n++;
    n0_nodo_t *n = &v->nodos[i];
    memset(n, 0, sizeof(*n));
    n->tipo = (uint8_t)t;
    n->hijo = -1;
    n->hermano = -1;
    n->sel = -1;
    if (padre >= 0 && padre < v->n) {
        n0_nodo_t *p = &v->nodos[padre];
        if (p->hijo < 0) {
            p->hijo = i;
        } else {
            int16_t h = p->hijo;
            while (v->nodos[h].hermano >= 0) h = v->nodos[h].hermano;
            v->nodos[h].hermano = i;
        }
    }
    return i;
}

int16_t n0_pantalla(n0_vista_t *v, const char *titulo) {
    int16_t i = nuevo(v, -1, N0_NODO_PANTALLA);
    if (i >= 0) v->nodos[i].a = titulo;
    return i;
}

int16_t n0_lista(n0_vista_t *v, int16_t padre, int16_t sel, int16_t pagina, int16_t paginas) {
    int16_t i = nuevo(v, padre, N0_NODO_LISTA);
    if (i >= 0) {
        v->nodos[i].sel = sel;
        v->nodos[i].pagina = pagina;
        v->nodos[i].paginas = paginas;
    }
    return i;
}

int16_t n0_fila(n0_vista_t *v, int16_t padre, const char *texto, const char *secundario, n0_marca_t m) {
    int16_t i = nuevo(v, padre, N0_NODO_FILA);
    if (i >= 0) {
        v->nodos[i].a = texto;
        v->nodos[i].b = secundario;
        v->nodos[i].marca = (uint8_t)m;
    }
    return i;
}

int16_t n0_kv(n0_vista_t *v, int16_t padre, const char *etiqueta, const char *valor) {
    int16_t i = nuevo(v, padre, N0_NODO_KV);
    if (i >= 0) {
        v->nodos[i].a = etiqueta;
        v->nodos[i].b = valor;
    }
    return i;
}

int16_t n0_texto(n0_vista_t *v, int16_t padre, const char *contenido, int16_t pagina, int16_t paginas) {
    int16_t i = nuevo(v, padre, N0_NODO_TEXTO);
    if (i >= 0) {
        v->nodos[i].a = contenido;
        v->nodos[i].pagina = pagina;
        v->nodos[i].paginas = paginas;
    }
    return i;
}

int16_t n0_pregunta(n0_vista_t *v, int16_t padre, const char *enunciado, int16_t sel) {
    int16_t i = nuevo(v, padre, N0_NODO_PREGUNTA);
    if (i >= 0) {
        v->nodos[i].a = enunciado;
        v->nodos[i].sel = sel;
    }
    return i;
}

int16_t n0_vacio(n0_vista_t *v, int16_t padre, const char *que_falta, const char *que_hacer) {
    int16_t i = nuevo(v, padre, N0_NODO_VACIO);
    if (i >= 0) {
        v->nodos[i].a = que_falta;
        v->nodos[i].c = que_hacer;
    }
    return i;
}

int16_t n0_ocupado(n0_vista_t *v, int16_t padre, const char *haciendo, int16_t paso, int16_t total) {
    int16_t i = nuevo(v, padre, N0_NODO_OCUPADO);
    if (i >= 0) {
        v->nodos[i].a = haciendo;
        v->nodos[i].pagina = paso;
        v->nodos[i].paginas = total;
    }
    return i;
}

int16_t n0_error(n0_vista_t *v, int16_t padre, const char *codigo, const char *frase, const char *que_hacer) {
    int16_t i = nuevo(v, padre, N0_NODO_ERROR);
    if (i >= 0) {
        v->nodos[i].a = codigo;
        v->nodos[i].b = frase;
        v->nodos[i].c = que_hacer;
    }
    return i;
}

int16_t n0_insignia(n0_vista_t *v, int16_t padre, n0_marca_t m) {
    int16_t i = nuevo(v, padre, N0_NODO_INSIGNIA);
    if (i >= 0) v->nodos[i].marca = (uint8_t)m;
    return i;
}

int16_t n0_barra(n0_vista_t *v, int16_t padre) {
    return nuevo(v, padre, N0_NODO_BARRA);
}

int16_t n0_accion(n0_vista_t *v, int16_t barra, n0_accion_t a, const char *etiqueta) {
    int16_t i = nuevo(v, barra, N0_NODO_ACCION);
    if (i >= 0) {
        v->nodos[i].accion = (uint8_t)a;
        v->nodos[i].a = etiqueta;
    }
    return i;
}

static bool vacio_str(const char *s) { return s == NULL || s[0] == '\0'; }

n0_vista_err_t n0_vista_validar(const n0_vista_t *v) {
    if (v->desbordado) return N0_VISTA_DESBORDADA;
    if (v->n == 0 || v->nodos[0].tipo != N0_NODO_PANTALLA) return N0_VISTA_SIN_PANTALLA;

    int acciones = 0;
    bool hay_atras = false;
    for (int16_t i = 0; i < v->n; i++) {
        const n0_nodo_t *n = &v->nodos[i];
        switch (n->tipo) {
            case N0_NODO_VACIO:
                // Un vacío sin "qué hacer" no es un estado vacío diseñado, es
                // una pantalla muerta.
                if (vacio_str(n->c)) return N0_VISTA_VACIO_SIN_SALIDA;
                break;
            case N0_NODO_ERROR:
                if (vacio_str(n->c)) return N0_VISTA_ERROR_SIN_SALIDA;
                break;
            case N0_NODO_ACCION:
                acciones++;
                if (n->accion == N0_ACT_ATRAS || n->accion == N0_ACT_ATRAS_LARGO) hay_atras = true;
                break;
            default:
                break;
        }
    }
    // Ninguna pantalla puede presentar cero acciones posibles.
    if (acciones == 0) return N0_VISTA_SIN_ACCIONES;
    if (!hay_atras) return N0_VISTA_SIN_VUELTA;
    return N0_VISTA_OK;
}

const char *n0_vista_err_str(n0_vista_err_t e) {
    switch (e) {
        case N0_VISTA_OK: return "ok";
        case N0_VISTA_DESBORDADA: return "el árbol pasa de 256 nodos";
        case N0_VISTA_SIN_PANTALLA: return "el árbol no empieza por una pantalla";
        case N0_VISTA_SIN_ACCIONES: return "la pantalla no ofrece ninguna acción";
        case N0_VISTA_VACIO_SIN_SALIDA: return "un estado vacío no dice qué hacer";
        case N0_VISTA_ERROR_SIN_SALIDA: return "un error no dice qué puede hacer el usuario";
        case N0_VISTA_SIN_VUELTA: return "la pantalla no tiene vuelta atrás";
    }
    return "desconocido";
}
