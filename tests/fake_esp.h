// PicoSing - a simulated ESP-01 for the tests (fake_esp.c)
#ifndef FAKE_ESP_H
#define FAKE_ESP_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct fesp_conn fesp_conn_t;
typedef struct
{
    const char *name;          // the host's name (DNS)
    uint8_t     ip;            // 10.0.0.<ip>
    uint16_t    port;
    bool        tls_only;      // plain TCP is refused
    bool        down;          // connections are refused
    void (*serve)(fesp_conn_t *c, const char *req, size_t len);   // the whole request is in
    void (*tick)(fesp_conn_t *c, uint32_t now);                   // (a reply that goes on)
    void (*gone)(fesp_conn_t *c);                                 // the connection closes
} fesp_host_t;

extern bool     fesp_present;
extern uint32_t fesp_latency_ms, fesp_tls_ms, fesp_opens, fesp_tls_opens;

void fesp_add_host(const fesp_host_t *h);
void fesp_host_down(const char *name, bool down);
void fesp_out(fesp_conn_t *c, const void *p, size_t n);
void fesp_outs(fesp_conn_t *c, const char *s);
void fesp_close(fesp_conn_t *c);             // after what was written
void **fesp_user(fesp_conn_t *c);
uint32_t fesp_now(void);
int  fesp_open_conns(void);
void fesp_tick(uint32_t now);                // every simulated millisecond
void fesp_reset(void);
#endif
