// Modo de sincronización del simulador: habla con el servidor de VERDAD.
//
// Lee el provision.json que escribió el servidor, sube lo que el aparato tenga
// pendiente, baja lo que le falte y lo fusiona. Es el camino completo entre
// los dos lados, con el sobre firmado y el antirreplay de por medio.
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../nucleo/sinc.h"

const n0_transporte_t *transporte_http(const char *host, const char *puerto);

static n0_dueno_t dueno(uint16_t tipo) {
    (void)tipo;
    return N0_DUENO_COMPARTIDO;
}

// Saca un campo de un JSON plano. No es un analizador de JSON: provision.json
// lo escribe nuestro propio servidor y tiene una forma conocida.
static int campo_json(const char *json, const char *clave, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", clave);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p = strchr(p + strlen(pat), ':');
    if (!p) return -1;
    p++;
    while (*p == ' ' || *p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '\n' && i + 1 < cap) out[i++] = *p++;
    out[i] = 0;
    return 0;
}

static int hex_a_bytes(const char *h, uint8_t *out, size_t cap, size_t *n) {
    size_t len = strlen(h);
    if (len % 2 || len / 2 > cap) return -1;
    for (size_t i = 0; i < len / 2; i++) {
        unsigned v = 0;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    *n = len / 2;
    return 0;
}

static uint8_t g_log[64 * 1024];
static uint8_t g_valores[32 * 1024];

int sincronizar(const char *ruta_provision, const char *host, const char *puerto) {
    FILE *f = fopen(ruta_provision, "rb");
    if (!f) {
        fprintf(stderr, "no se pudo abrir %s\n", ruta_provision);
        return 2;
    }
    static char json[4096];
    size_t n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = 0;

    char dev[32], sec_hex[160];
    if (campo_json(json, "device_id", dev, sizeof(dev)) != 0 ||
        campo_json(json, "secret_hex", sec_hex, sizeof(sec_hex)) != 0) {
        fprintf(stderr, "provision.json no tiene device_id o secret_hex\n");
        return 2;
    }
    uint8_t secreto[64];
    size_t n_sec = 0;
    if (hex_a_bytes(sec_hex, secreto, sizeof(secreto), &n_sec) != 0) {
        fprintf(stderr, "secret_hex ilegible\n");
        return 2;
    }
    uint64_t device_id = strtoull(dev, NULL, 10);
    printf("aparato %llu, secreto de %zu bytes\n", (unsigned long long)device_id, n_sec);

    n0_almacen_t al;
    n0_almacen_init(&al, g_log, sizeof(g_log), device_id, dueno, g_valores, sizeof(g_valores));

    // El aparato escribe algo suyo antes de hablar: así la subida lleva carga
    // y se comprueba el camino entero, no solo la bajada.
    const char *suyas[] = {"Pilas de botón", "Café descafeinado"};
    for (size_t i = 0; i < sizeof(suyas) / sizeof(suyas[0]); i++) {
        n0_entidad_t e = n0_almacen_crear(&al, 10);
        n0_almacen_fijar(&al, e, 10, 1, suyas[i], (uint16_t)strlen(suyas[i]));
    }
    printf("el aparato tiene %zu bytes propios pendientes de subir\n", al.n);

    n0_sinc_t s;
    n0_sinc_init(&s, device_id, 2001, secreto, n_sec);
    const n0_transporte_t *t = transporte_http(host, puerto);

    n0_sinc_res_t r = n0_sinc_ciclo(&s, &al, t);
    printf("ciclo 1: %s (HTTP %d)\n", n0_sinc_str(r), s.ultimo_http);
    if (r != N0_SINC_OK) return 1;
    printf("  subido hasta %zu bytes, recibido hasta lamport %llu, %d entidades\n",
           s.enviado_bytes, (unsigned long long)s.recibido_hasta, al.fus.n);

    // Segundo ciclo: no hay nada nuevo que subir, y el antirreplay tiene que
    // aceptarlo porque el sobre_seq sube aunque el lote vaya vacío.
    int ents1 = al.fus.n;
    r = n0_sinc_ciclo(&s, &al, t);
    printf("ciclo 2: %s (HTTP %d)\n", n0_sinc_str(r), s.ultimo_http);
    if (r != N0_SINC_OK) return 1;
    if (al.fus.n != ents1) {
        printf("  FALLO: el segundo ciclo cambió el número de entidades (%d -> %d)\n",
               ents1, al.fus.n);
        return 1;
    }
    printf("  el segundo ciclo no duplica nada: siguen %d entidades\n", al.fus.n);

    // Y un sobre con una firma mala tiene que dar 401 con la hora dentro.
    n0_sinc_t malo;
    uint8_t otro[32];
    memset(otro, 0xAB, sizeof(otro));
    n0_sinc_init(&malo, device_id, 2001, otro, sizeof(otro));
    r = n0_sinc_ciclo(&malo, &al, t);
    printf("firma mala: %s (HTTP %d), hora del servidor %lld\n",
           n0_sinc_str(r), malo.ultimo_http, (long long)malo.hora_servidor);
    if (r != N0_SINC_NO_AUTORIZADO || malo.hora_servidor == 0) {
        printf("  FALLO: el 401 tiene que llevar la hora del servidor\n");
        return 1;
    }

    printf("\nsincronización de extremo a extremo: correcta\n");
    return 0;
}
