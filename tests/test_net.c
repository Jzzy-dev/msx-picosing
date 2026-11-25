// PicoSing - test_net: the HTTP client (web.c) over the ESP link (esp.c),
// against a simulated ESP-01 and small servers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_esp.h"
#include "../pico/net/web.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t now;
static char got[100000];
static size_t got_len;
static int done_n, done_ok;
static char last_req[2000];

static void serve(fesp_conn_t *c, const char *req, size_t len)
{
    snprintf(last_req, sizeof last_req, "%.*s", (int)len, req);
    if (strstr(req, "GET /hello "))
        fesp_outs(c, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nSet-Cookie: SID=abc123; path=/\r\n\r\nhello world"), fesp_close(c);
    else if (strstr(req, "POST /form "))
    {
        const char *b = strstr(req, "\r\n\r\n") + 4;
        char r[300];
        snprintf(r, sizeof r, "HTTP/1.0 302 Found\r\nLocation: /after?got=%s\r\nSet-Cookie: B=2\r\n\r\n", b);
        fesp_outs(c, r);
        fesp_close(c);
    }
    else if (strstr(req, "GET /after"))
    {
        char r[600];
        const char *ck = strstr(req, "Cookie: ");
        snprintf(r, sizeof r, "HTTP/1.0 200 OK\r\n\r\n[%.*s]", ck ? (int)strcspn(ck, "\r") : 0, ck ? ck : "");
        fesp_outs(c, r);
        fesp_close(c);
    }
    else if (strstr(req, "GET /big "))
    {
        fesp_outs(c, "HTTP/1.0 200 OK\r\nContent-Length: 50000\r\n\r\n");
        for (int i = 0; i < 50000; i++) { char ch = (char)('a' + i % 26); fesp_out(c, &ch, 1); }
    }
    else if (strstr(req, "GET /stream "))
        fesp_outs(c, "HTTP/1.0 200 OK\r\n\r\n"), *fesp_user(c) = (void *)(uintptr_t)1;      // then the tick
    else fesp_outs(c, "HTTP/1.0 404 Not Found\r\n\r\n"), fesp_close(c);
}
static void tick(fesp_conn_t *c, uint32_t t)
{
    void **u = fesp_user(c);
    uintptr_t last = (uintptr_t)*u;
    if (last && t - last >= 500) { char l[32]; snprintf(l, sizeof l, "T %u\n", t); fesp_outs(c, l); *u = (void *)(uintptr_t)t; }
}

static bool body(web_t *w, const uint8_t *p, size_t n) { (void)w; if (got_len + n < sizeof got) { memcpy(got + got_len, p, n); got_len += n; got[got_len] = 0; } return false; }
static void done(web_t *w, bool ok) { (void)w; done_n++; done_ok = ok; }

static void run(web_t *w, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) { now++; fesp_tick(now); web_poll(w); }
}

int main(void)
{
    fesp_host_t h = { "test.host", 30, 80, false, false, serve, tick, NULL };
    fesp_add_host(&h);
    fesp_host_t hs = { "secure.host", 31, 443, true, false, serve, tick, NULL };
    fesp_add_host(&hs);
    static web_t w, w2;
    web_init(&w);
    web_init(&w2);

    got_len = 0; done_n = 0;
    CHECK(web_start(&w, "http://test.host/hello", NULL, 0, body, done, false), "start");
    run(&w, 3000);
    CHECK(done_n == 1 && done_ok && !strcmp(got, "hello world"), "GET: %d %d '%s'", done_n, done_ok, got);
    CHECK(!strcmp(w.cookies, "SID=abc123"), "cookie kept: '%s'", w.cookies);

    // a POST that redirects: the cookie goes along, the form arrives
    got_len = 0; done_n = 0;
    char form[64];
    web_escape(form, sizeof form, "a b&c");
    char b2[80]; snprintf(b2, sizeof b2, "user=%s&x=1", form);
    CHECK(web_start(&w, "https://secure.host/form", b2, strlen(b2), body, done, false), "post");
    run(&w, 4000);
    CHECK(done_n == 1 && done_ok && !strcmp(got, "[Cookie: SID=abc123; B=2]"), "POST+redirect: %d '%s'", done_ok, got);
    CHECK(strstr(last_req, "GET /after?got=user=a+b%26c&x=1"), "the form: %s", last_req);

    // 50 KB
    got_len = 0; done_n = 0;
    web_start(&w, "http://test.host/big", NULL, 0, body, done, false);
    run(&w, 10000);
    CHECK(done_n == 1 && done_ok && got_len == 50000 && got[49999] == 'a' + 49999 % 26, "big: %zu", got_len);

    // 404
    done_n = 0;
    web_start(&w, "http://test.host/nothing", NULL, 0, body, done, false);
    run(&w, 3000);
    CHECK(done_n == 1 && !done_ok && web_http(&w) == 404, "404: %u %s", web_http(&w), web_error_text(&w));

    // a stream that goes on, with a request beside it
    got_len = 0; done_n = 0;
    web_start(&w, "http://test.host/stream", NULL, 0, body, done, true);
    for (int i = 0; i < 80000; i++)
    {
        now++; fesp_tick(now); web_poll(&w); web_poll(&w2);
        if (i == 40000) web_start(&w2, "http://test.host/hello", NULL, 0, NULL, done, false);
    }
    int lines = 0; for (char *p = got; (p = strchr(p, '\n')); p++) lines++;
    CHECK(lines >= 150 && done_n == 1 && web_busy(&w), "stream: %d lines, %d done, busy %d", lines, done_n, web_busy(&w));
    web_abort(&w);
    run(&w, 3000);
    CHECK(!web_busy(&w) && fesp_open_conns() == 0, "aborted, %d open", fesp_open_conns());

    // unknown host
    done_n = 0;
    web_start(&w, "http://nowhere.test/", NULL, 0, body, done, false);
    run(&w, 3000);
    CHECK(done_n == 1 && !done_ok && web_error(&w) == WEB_E_DNS, "dns: %s", web_error_text(&w));
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
