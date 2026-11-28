// Prueba cruzada: el núcleo en C fusiona el MISMO registro que el servidor en
// Go y tiene que llegar EXACTAMENTE al mismo estado.
//
// Es lo que convierte "los dos lados convergen" de esperanza en hecho
// comprobable. Si esta prueba se rompe, la sincronización está rota de una
// forma que en producción no se nota hasta meses después, cuando ya hay
// historial que perder.
//
// El material lo genera el servidor:
//   NITR0S_FIXTURE=$PWD/firmware/pruebas/datos go test ./internal/merge/ -run Fixture
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../nucleo/fusion.h"

#define MAX_LOG (1 << 20)
#define MAX_TXT (1 << 16)

static n0_dueno_t dueno(uint16_t tipo) {
    return tipo == 10 ? N0_DUENO_COMPARTIDO : N0_DUENO_COMPARTIDO;
}

static size_t leer(const char *ruta, uint8_t *dst, size_t cap) {
    FILE *f = fopen(ruta, "rb");
    if (!f) return 0;
    size_t n = fread(dst, 1, cap, f);
    fclose(f);
    return n;
}

// Vuelca el estado en el MISMO formato que volcar() del servidor. Carácter por
// carácter: si no coincidiera el formato, la comparación no probaría nada.
static void volcar(const n0_fusion_t *f, char *out, size_t cap) {
    size_t o = 0;
    for (int i = 0; i < f->n; i++) {
        const n0_ent_t *e = &f->ents[i];
        o += (size_t)snprintf(out + o, cap - o, "%llu-%llu tipo=%u creada=%d borrada=%d\n",
                              (unsigned long long)e->id.creador, (unsigned long long)e->id.seq,
                              e->tipo, e->creada ? 1 : 0, n0_ent_borrada(e) ? 1 : 0);
        for (int c = 0; c < e->n_campos; c++) {
            const n0_op_t *op = &e->campos[c].op;
            o += (size_t)snprintf(out + o, cap - o, "  campo %u v%llu autor=%llu/%u len=%u hex=",
                                  e->campos[c].campo, (unsigned long long)op->version_campo,
                                  (unsigned long long)op->autor, op->autor_tipo, op->n_valor);
            for (uint16_t b = 0; b < op->n_valor; b++)
                o += (size_t)snprintf(out + o, cap - o, "%02x", op->valor[b]);
            o += (size_t)snprintf(out + o, cap - o, "\n");
        }
    }
    for (int i = 0; i < f->n_conflictos; i++) {
        const n0_conflicto_t *c = &f->conflictos[i];
        o += (size_t)snprintf(out + o, cap - o, "conflicto %llu-%llu campo=%u autor=%u perdedor=",
                              (unsigned long long)c->entidad.creador,
                              (unsigned long long)c->entidad.seq, c->campo, c->autor_perdedor);
        for (uint16_t b = 0; b < c->n_perdedor; b++)
            o += (size_t)snprintf(out + o, cap - o, "%02x", c->perdedor[b]);
        o += (size_t)snprintf(out + o, cap - o, " motivo=%s\n", c->motivo);
    }
    out[o] = 0;
}

static int comparar(const char *a, const char *b) {
    if (strcmp(a, b) == 0) return 0;
    // Enseñar la PRIMERA línea que difiere: un diff de dos volcados enteros no
    // dice nada útil.
    int linea = 1;
    const char *pa = a, *pb = b;
    while (*pa && *pb) {
        const char *fa = strchr(pa, '\n');
        const char *fb = strchr(pb, '\n');
        size_t la = fa ? (size_t)(fa - pa) : strlen(pa);
        size_t lb = fb ? (size_t)(fb - pb) : strlen(pb);
        if (la != lb || memcmp(pa, pb, la) != 0) {
            printf("  primera diferencia en la línea %d:\n", linea);
            printf("    go: %.*s\n", (int)la, pa);
            printf("    c : %.*s\n", (int)lb, pb);
            return 1;
        }
        if (!fa || !fb) break;
        pa = fa + 1;
        pb = fb + 1;
        linea++;
    }
    printf("  los volcados difieren en longitud: go=%zu c=%zu\n", strlen(a), strlen(b));
    return 1;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "pruebas/datos";
    char ruta[512];
    static uint8_t log[MAX_LOG];
    static char esperado[MAX_TXT];
    static char obtenido[MAX_TXT];

    snprintf(ruta, sizeof(ruta), "%s/registro.bin", dir);
    size_t n = leer(ruta, log, sizeof(log));
    if (n == 0) {
        printf("no se pudo leer %s; genera el material con el servidor\n", ruta);
        return 2;
    }
    snprintf(ruta, sizeof(ruta), "%s/estado_go.txt", dir);
    size_t ne = leer(ruta, (uint8_t *)esperado, sizeof(esperado) - 1);
    if (ne == 0) {
        printf("no se pudo leer %s\n", ruta);
        return 2;
    }
    esperado[ne] = 0;

    // 1. La pasada de recuperación tiene que aceptar el registro entero.
    int n_ops = 0, sin_interpretar = 0;
    size_t valido = n0_op_recuperar(log, n, &n_ops, &sin_interpretar);
    printf("prueba cruzada: %zu bytes, %d operaciones, prefijo válido %zu\n", n, n_ops, valido);
    if (valido != n) {
        printf("  FALLO: el prefijo válido no cubre el registro entero\n");
        return 1;
    }

    // 2. Fusionar y comparar con lo que produjo el servidor.
    n0_fusion_t f;
    n0_fusion_init(&f, dueno);
    n0_fusion_aplicar_log(&f, log, n);
    if (f.desbordado) {
        printf("  FALLO: la fusión se desbordó\n");
        return 1;
    }
    n0_fusion_ordenar(&f);
    volcar(&f, obtenido, sizeof(obtenido));

    if (comparar(esperado, obtenido) != 0) {
        printf("  FALLO: el núcleo en C y el servidor en Go NO convergen\n");
        return 1;
    }
    printf("  el núcleo en C y el servidor en Go convergen al mismo estado\n");

    // 3. Y aplicar el registro dos veces no puede cambiar nada: la fusión es
    //    idempotente, que es lo que hace seguro reenviar un lote.
    n0_fusion_t g;
    n0_fusion_init(&g, dueno);
    n0_fusion_aplicar_log(&g, log, n);
    n0_fusion_aplicar_log(&g, log, n);
    n0_fusion_ordenar(&g);
    static char dos_veces[MAX_TXT];
    volcar(&g, dos_veces, sizeof(dos_veces));
    // Los conflictos sí se anotan dos veces; el ESTADO no puede cambiar.
    const char *corte_a = strstr(obtenido, "conflicto ");
    const char *corte_b = strstr(dos_veces, "conflicto ");
    size_t la = corte_a ? (size_t)(corte_a - obtenido) : strlen(obtenido);
    size_t lb = corte_b ? (size_t)(corte_b - dos_veces) : strlen(dos_veces);
    if (la != lb || memcmp(obtenido, dos_veces, la) != 0) {
        printf("  FALLO: aplicar el registro dos veces cambia el estado\n");
        return 1;
    }
    printf("  y aplicarlo dos veces no cambia el estado\n");

    // 4. Truncar en cada byte nunca puede interpretar basura.
    for (size_t corte = 0; corte <= n; corte++) {
        int ops2 = 0;
        size_t v = n0_op_recuperar(log, corte, &ops2, NULL);
        if (v > corte || ops2 > n_ops) {
            printf("  FALLO: corte en %zu da un prefijo inválido\n", corte);
            return 1;
        }
    }
    printf("  y un corte en cualquier byte deja siempre un prefijo válido\n");
    return 0;
}
