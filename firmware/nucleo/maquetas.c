// maquetas — propuestas de disposición para la fase visual.
//
// NINGUNA ESTÁ APROBADA. Son material para que el autor compare, que es
// exactamente lo que pide §12.3 del brief: dos o tres propuestas de la pantalla
// de lista, que es la que manda porque las tres apps de v1 son listas.
//
// Todas respetan las restricciones FUNCIONALES de §12.1, que no son
// negociables: 1 bit, sin grises, sin trazos finos para nada importante, se
// pagina y no se hace scroll, y el acuse de una pulsación cabe en la región
// más pequeña posible.
#include "render.h"

extern const n0_fuente_t n0_fuente_cuerpo;
extern const n0_fuente_t n0_fuente_titulo;

// A — densa. Más filas por pantalla, reglas finas entre ellas, selección por
// barra invertida. Pensada para la lista de la compra en el supermercado, donde
// lo que se quiere es ver muchas cosas de un vistazo.
const n0_maqueta_t n0_maqueta_densa = {
    .nombre = "densa",
    .descripcion = "más filas por página, reglas entre filas, selección invertida",
    .margen_x = 16,
    .margen_y = 10,
    .alto_cabecera = 44,
    .alto_fila = 38,
    .alto_barra = 34,
    .sangria_marca = 26,
    .regla_cabecera = 1,
    .regla_filas = 1,
    .sel_invertida = 1,
    .barra_arriba = 0,
    .f_titulo = &n0_fuente_titulo,
    .f_cuerpo = &n0_fuente_cuerpo,
};

// B — holgada. Menos filas, más aire, sin reglas. Pensada para leerse de un
// vistazo con el aparato en la mano y con poca luz, que es la restricción real
// de este panel: sin luz frontal, el contraste y el tamaño son todo.
const n0_maqueta_t n0_maqueta_holgada = {
    .nombre = "holgada",
    .descripcion = "menos filas, más aire, sin reglas, selección invertida",
    .margen_x = 28,
    .margen_y = 18,
    .alto_cabecera = 56,
    .alto_fila = 56,
    .alto_barra = 40,
    .sangria_marca = 34,
    .regla_cabecera = 1,
    .regla_filas = 0,
    .sel_invertida = 1,
    .barra_arriba = 0,
    .f_titulo = &n0_fuente_titulo,
    .f_cuerpo = &n0_fuente_cuerpo,
};

// C — cuña arriba. Barra de etiquetas ARRIBA y selección marcada con una cuña
// lateral en vez de invirtiendo la fila. Existe para poner a prueba una
// pregunta concreta: invertir una fila entera deja más fantasma que pintar una
// cuña de 4 píxeles, y eso solo se puede juzgar en el panel real.
const n0_maqueta_t n0_maqueta_cuna = {
    .nombre = "cuna",
    .descripcion = "barra de etiquetas arriba, selección por cuña lateral",
    .margen_x = 20,
    .margen_y = 12,
    .alto_cabecera = 48,
    .alto_fila = 44,
    .alto_barra = 36,
    .sangria_marca = 30,
    .regla_cabecera = 1,
    .regla_filas = 0,
    .sel_invertida = 0,
    .barra_arriba = 1,
    .f_titulo = &n0_fuente_titulo,
    .f_cuerpo = &n0_fuente_cuerpo,
};

const n0_maqueta_t *const n0_maquetas[] = {
    &n0_maqueta_densa,
    &n0_maqueta_holgada,
    &n0_maqueta_cuna,
};
const int n0_n_maquetas = 3;
