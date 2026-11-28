// Transporte HTTP del simulador: sockets POSIX y un cliente HTTP/1.1 mínimo.
//
// En el aparato esto será esp_http_client sobre TLS. La costura es
// n0_transporte_t, así que el ciclo de sincronización no sabe cuál de los dos
// hay debajo, y es el MISMO código el que se prueba aquí y el que correrá allí.
#define _POSIX_C_SOURCE 200112L

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../nucleo/sinc.h"

typedef struct {
    char host[128];
    char puerto[8];
} http_ctx_t;

static http_ctx_t g_ctx;

static int conectar(const char *host, const char *puerto) {
    struct addrinfo pistas, *res = NULL;
    memset(&pistas, 0, sizeof(pistas));
    pistas.ai_family = AF_UNSPEC;
    pistas.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, puerto, &pistas, &res) != 0) return -1;
    int fd = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int escribir_todo(int fd, const uint8_t *d, size_t n) {
    while (n > 0) {
        ssize_t k = write(fd, d, n);
        if (k <= 0) return -1;
        d += k;
        n -= (size_t)k;
    }
    return 0;
}

static int post(void *ctx, const char *ruta, const uint8_t *cuerpo, size_t n,
                uint8_t *resp, size_t cap, size_t *n_resp, int *http) {
    http_ctx_t *c = (http_ctx_t *)ctx;
    int fd = conectar(c->host, c->puerto);
    if (fd < 0) return -1;

    char cab[512];
    int n_cab = snprintf(cab, sizeof(cab),
                         "POST %s HTTP/1.1\r\n"
                         "Host: %s:%s\r\n"
                         "Content-Type: application/octet-stream\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n\r\n",
                         ruta, c->host, c->puerto, n);
    if (escribir_todo(fd, (const uint8_t *)cab, (size_t)n_cab) != 0 ||
        escribir_todo(fd, cuerpo, n) != 0) {
        close(fd);
        return -1;
    }

    static uint8_t buf[128 * 1024];
    size_t total = 0;
    for (;;) {
        ssize_t k = read(fd, buf + total, sizeof(buf) - total);
        if (k <= 0) break;
        total += (size_t)k;
        if (total >= sizeof(buf)) break;
    }
    close(fd);
    if (total < 12) return -1;

    // Estado y separación de cabeceras y cuerpo. Suficiente para hablar con
    // nuestro propio servidor; no pretende ser un cliente HTTP general.
    if (sscanf((const char *)buf, "HTTP/1.%*d %d", http) != 1) return -1;
    const char *fin = strstr((const char *)buf, "\r\n\r\n");
    if (!fin) return -1;
    size_t off = (size_t)((const uint8_t *)fin - buf) + 4;
    *n_resp = total - off;
    if (*n_resp > cap) *n_resp = cap;
    memcpy(resp, buf + off, *n_resp);
    return 0;
}

const n0_transporte_t *transporte_http(const char *host, const char *puerto) {
    static n0_transporte_t t;
    snprintf(g_ctx.host, sizeof(g_ctx.host), "%s", host);
    snprintf(g_ctx.puerto, sizeof(g_ctx.puerto), "%s", puerto);
    t.post = post;
    t.ctx = &g_ctx;
    return &t;
}
