// PicoSing - a simulated ESP-01 (the UNAPI ESP8266 firmware) for the tests
//
// It reads the requests esp.c writes, answers them after a short delay, and
// connects TCP connections to servers written in C (fake_esp.h): each sees
// the whole HTTP request and writes its reply, at once or bit by bit.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_esp.h"
#include "../pico/net/net_int.h"

bool     fesp_present = true;
uint32_t fesp_latency_ms = 3;
uint32_t fesp_tls_ms = 300;          // a TLS handshake
uint32_t fesp_opens, fesp_tls_opens;

typedef struct { uint8_t *b; size_t len, cap; } bq_t;
static void bq_put(bq_t *q, const void *p, size_t n)
{
    if (!n) return;
    if (q->len + n > q->cap) { q->cap = (q->len + n) * 2 + 256; q->b = realloc(q->b, q->cap); }
    memcpy(q->b + q->len, p, n);
    q->len += n;
}
static void bq_drop(bq_t *q, size_t n) { if (n > q->len) n = q->len; memmove(q->b, q->b + n, q->len - n); q->len -= n; }

typedef struct ans { uint32_t at; bq_t data; struct ans *next; } ans_t;
static ans_t *answers;
static bq_t uart_rx, tx;
static bool owned;
static uint32_t now_ms;

struct fesp_conn
{
    bool used, tls, closed, got;
    const fesp_host_t *host;
    bq_t req, out;
    uint32_t opened, last;
    void *user;
};
static fesp_conn_t conns[5];
static fesp_host_t hosts[8];
static int nhosts;

void fesp_add_host(const fesp_host_t *h) { if (nhosts < 8) hosts[nhosts++] = *h; }
void fesp_host_down(const char *name, bool down) { for (int i = 0; i < nhosts; i++) if (!strcmp(hosts[i].name, name)) hosts[i].down = down; }
void fesp_out(fesp_conn_t *c, const void *p, size_t n) { bq_put(&c->out, p, n); }
void fesp_outs(fesp_conn_t *c, const char *s) { bq_put(&c->out, s, strlen(s)); }
void fesp_close(fesp_conn_t *c) { c->closed = true; }
void **fesp_user(fesp_conn_t *c) { return &c->user; }
uint32_t fesp_now(void) { return now_ms; }
int fesp_open_conns(void) { int n = 0; for (int i = 1; i <= 4; i++) n += conns[i].used; return n; }

static void answer(uint32_t delay, uint8_t fn, uint8_t rc, const void *p, size_t n)
{
    ans_t *a = calloc(1, sizeof *a);
    a->at = now_ms + delay;
    uint8_t h[4] = { fn, rc, (uint8_t)(n >> 8), (uint8_t)n };
    bq_put(&a->data, h, 4);
    if (rc == 0 && n) bq_put(&a->data, p, n);
    ans_t **pp = &answers;
    while (*pp) pp = &(*pp)->next;
    *pp = a;
}

static void free_conn(fesp_conn_t *c)
{
    if (c->host && c->host->gone) c->host->gone(c);
    free(c->req.b);
    free(c->out.b);
    memset(c, 0, sizeof *c);
}

static void request(const uint8_t *p, size_t n)
{
    uint8_t fn = p[0];
    const uint8_t *a = p + 3;
    size_t len = n - 3;
    uint32_t L = fesp_latency_ms;
    switch (fn)
    {
        case ESP_FN_DNS:
        {
            char name[80] = "";
            size_t k = len - 1 < sizeof name - 1 ? len - 1 : sizeof name - 1;
            memcpy(name, a + 1, k);
            name[k] = 0;
            for (int i = 0; i < nhosts; i++)
                if (!strcmp(hosts[i].name, name))
                {
                    uint8_t ip[4] = { 10, 0, 0, hosts[i].ip };
                    answer(L, fn, 0, ip, 4);
                    return;
                }
            // a dotted address
            unsigned b[4];
            if (sscanf(name, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) == 4)
            {
                uint8_t ip[4] = { (uint8_t)b[0], (uint8_t)b[1], (uint8_t)b[2], (uint8_t)b[3] };
                answer(L, fn, 0, ip, 4);
                return;
            }
            answer(L, fn, 2, NULL, 0);
            return;
        }
        case ESP_FN_TCPOPEN:
        {
            int slot = 0;
            for (int i = 1; i <= 4; i++) if (!conns[i].used) { slot = i; break; }
            bool tls = (a[10] & 0x04) != 0;
            bool tls_busy = false;
            for (int i = 1; i <= 4; i++) if (conns[i].used && conns[i].tls) tls_busy = true;
            if (!slot || (tls && tls_busy)) { answer(L, fn, 9, NULL, 0); return; }
            uint16_t port = (uint16_t)(a[4] | (a[5] << 8));
            const fesp_host_t *h = NULL;
            for (int i = 0; i < nhosts; i++)
                if (a[0] == 10 && a[3] == hosts[i].ip && port == hosts[i].port && !hosts[i].down) h = &hosts[i];
            if (!h || (h->tls_only && !tls)) { answer(L, fn, 3, NULL, 0); return; }
            fesp_conn_t *c = &conns[slot];
            memset(c, 0, sizeof *c);
            c->used = true;
            c->tls = tls;
            c->host = h;
            c->opened = now_ms;
            fesp_opens++;
            if (tls) fesp_tls_opens++;
            uint8_t cn = (uint8_t)slot;
            answer(L + (tls ? fesp_tls_ms : 0), fn, 0, &cn, 1);
            return;
        }
        case ESP_FN_TCPCLOSE:
        case ESP_FN_TCPABORT:
        {
            uint8_t cn = a[0];
            if (cn >= 1 && cn <= 4 && conns[cn].used) { free_conn(&conns[cn]); answer(L, fn, 0, NULL, 0); }
            else answer(L, fn, 11, NULL, 0);
            return;
        }
        case ESP_FN_TCPSEND:
        {
            uint8_t cn = a[0];
            if (cn < 1 || cn > 4 || !conns[cn].used) { answer(L, fn, 11, NULL, 0); return; }
            fesp_conn_t *c = &conns[cn];
            bq_put(&c->req, a + 2, len - 2);
            uint8_t z = 0;
            bq_put(&c->req, &z, 1);
            c->req.len--;
            const char *eoh = strstr((char *)c->req.b, "\r\n\r\n");
            const char *cl = strcasestr((char *)c->req.b, "Content-Length: ");
            bool whole = eoh && (!cl || cl > eoh || c->req.len >= (size_t)(eoh + 4 - (char *)c->req.b) + (size_t)atoi(cl + 16));
            if (!c->got && whole) { c->got = true; c->host->serve(c, (const char *)c->req.b, c->req.len); }
            answer(L, fn, 0, NULL, 0);
            return;
        }
        case ESP_FN_TCPRCV:
        {
            uint8_t cn = a[0];
            uint16_t want = (uint16_t)(a[1] | (a[2] << 8));
            if (cn < 1 || cn > 4 || !conns[cn].used) { answer(L, fn, 11, NULL, 0); return; }
            fesp_conn_t *c = &conns[cn];
            if (c->out.len == 0 && c->closed) { answer(L, fn, 11, NULL, 0); return; }
            size_t k = c->out.len < want ? c->out.len : want;
            uint8_t *buf = malloc(k + 2);
            buf[0] = buf[1] = 0;
            memcpy(buf + 2, c->out.b, k);
            bq_drop(&c->out, k);
            answer(L, fn, 0, buf, k + 2);
            free(buf);
            return;
        }
        case ESP_FN_TCPSTATE:
        {
            uint8_t cn = a[0];
            if (cn < 1 || cn > 4 || !conns[cn].used) { answer(L, fn, 11, NULL, 0); return; }
            fesp_conn_t *c = &conns[cn];
            uint8_t r[16] = { 0 };
            r[1] = c->closed ? 7 : 4;
            r[2] = (uint8_t)c->out.len;
            r[3] = (uint8_t)(c->out.len >> 8);
            answer(L, fn, 0, r, 16);
            return;
        }
        default:
            answer(L, fn, 1, NULL, 0);
    }
}

static void parse_tx(void)
{
    while (tx.len >= 3)
    {
        size_t len = ((size_t)tx.b[1] << 8) | tx.b[2];
        if (tx.len < 3 + len) return;
        request(tx.b, 3 + len);
        bq_drop(&tx, 3 + len);
    }
}

void fesp_tick(uint32_t now)
{
    uint32_t ms = now > now_ms ? now - now_ms : 1;
    now_ms = now;
    for (int i = 1; i <= 4; i++)
        if (conns[i].used && conns[i].got && conns[i].host->tick && !conns[i].closed)
            conns[i].host->tick(&conns[i], now);
    // about 80 bytes a millisecond on the wire (859 kbaud)
    size_t budget = 80 * (size_t)(ms > 50 ? 50 : ms);
    while (answers && answers->at <= now_ms && budget)
    {
        ans_t *a = answers;
        size_t k = a->data.len < budget ? a->data.len : budget;
        bq_put(&uart_rx, a->data.b, k);
        bq_drop(&a->data, k);
        budget -= k;
        if (!a->data.len) { answers = a->next; free(a->data.b); free(a); }
    }
}

void fesp_reset(void)
{
    for (int i = 1; i <= 4; i++) if (conns[i].used) free_conn(&conns[i]);
    while (answers) { ans_t *a = answers; answers = a->next; free(a->data.b); free(a); }
    uart_rx.len = tx.len = 0;
}

// ---------------- the HAL esp.c uses ----------------
uint32_t nethal_millis(void) { return now_ms; }
bool nethal_present(void) { return fesp_present; }
bool nethal_open(void) { if (!fesp_present) return false; owned = true; return true; }
void nethal_close(void) { owned = false; uart_rx.len = 0; }
size_t nethal_write(const uint8_t *p, size_t n)
{
    if (!owned) return 0;
    bq_put(&tx, p, n);
    parse_tx();
    return n;
}
size_t nethal_read(uint8_t *p, size_t n)
{
    if (!owned) return 0;
    if (n > uart_rx.len) n = uart_rx.len;
    if (!n) return 0;
    memcpy(p, uart_rx.b, n);
    bq_drop(&uart_rx, n);
    return n;
}
