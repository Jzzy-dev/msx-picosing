// PicoSing - karaoke for the MSX PicoVerse 2350
// web.c - HTTP(S) requests over the ESP-01 (see web.h).
//
// After PicoAmp's pva_web.c (one GET at a time), made into one client per
// web_t, with form POSTs and cookies. Each client has a slot of its own on
// the shared ESP link (esp.c), so the PC's screen can stream while a
// command or a USDB page goes back and forth.
//
// This work is licensed under a "Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
// License". https://creativecommons.org/licenses/by-nc-sa/4.0/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "web.h"

#define MAX_REDIRECTS   4u
#define RCV_SIZE        1024u
#define T_DNS_MS        15000u
#define T_OPEN_MS       20000u
#define T_SEND_MS       3000u
#define T_RCV_MS        2000u
#define T_QUERY_MS      60000u
#define T_SILENT_MS     12000u
#define IDLE_POLL_MS    4u
#define QUIET_MS        400u
#define AGENT           "PicoSing/0.7 (MSX PicoVerse 2350)"

enum { W_IDLE, W_DNS, W_OPEN, W_SEND, W_HEAD, W_BODY, W_CLOSE };
enum { A_DONE, A_FAIL, A_REDIRECT };

bool web_busy(const web_t *w) { return w->st != W_IDLE; }
uint16_t web_http(const web_t *w) { return w->http; }
uint8_t web_error(const web_t *w) { return w->err; }

const char *web_error_text(const web_t *w)
{
    static char t[32];
    switch (w->err)
    {
        case WEB_E_NOWIFI:  return "NO WIFI";
        case WEB_E_DNS:     return "UNKNOWN HOST";
        case WEB_E_CONNECT: return "NO CONNECTION";
        case WEB_E_TIMEOUT: return "NO ANSWER";
        case WEB_E_URL:     return "BAD ADDRESS";
        case WEB_E_CLOSED:  return "CONNECTION LOST";
        case WEB_E_HTTP:    snprintf(t, sizeof t, "SERVER SAYS %u", (unsigned)w->http); return t;
        default:            return "";
    }
}

bool web_escape(char *out, size_t size, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            if (o + 1 >= size) return false;
            out[o++] = (char)c;
        }
        else if (c == ' ')
        {
            if (o + 1 >= size) return false;
            out[o++] = '+';
        }
        else
        {
            if (o + 3 >= size) return false;
            out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
    return true;
}

// ---------------------------------------------------------------- cookies
static void keep_cookie(web_t *w, const char *v)
{
    // "NAME=VALUE; Path=/..." -> NAME=VALUE into the jar (replacing the old one)
    char nv[160];
    size_t k = strcspn(v, ";");
    if (k == 0 || k >= sizeof nv) return;
    memcpy(nv, v, k);
    nv[k] = 0;
    char *eq = strchr(nv, '=');
    if (!eq) return;
    size_t nl = (size_t)(eq - nv) + 1;          // "NAME="
    char jar[sizeof w->cookies] = "";
    const char *p = w->cookies;
    while (*p)
    {
        const char *e = strstr(p, "; ");
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (!(l >= nl && !strncmp(p, nv, nl)) && strlen(jar) + l + 3 < sizeof jar)
        {
            if (jar[0]) strcat(jar, "; ");
            strncat(jar, p, l);
        }
        p += l;
        if (*p) p += 2;
    }
    bool deleted = !strcmp(eq + 1, "deleted") || eq[1] == 0;
    if (!deleted && strlen(jar) + strlen(nv) + 3 < sizeof jar)
    {
        if (jar[0]) strcat(jar, "; ");
        strcat(jar, nv);
    }
    snprintf(w->cookies, sizeof w->cookies, "%s", jar);
}

// ---------------------------------------------------------------- the reply
static void close_then(web_t *w, uint8_t what)
{
    w->after = what;
    if (w->conn)
    {
        uint8_t c = w->conn;
        esp_request(&w->slot, ESP_FN_TCPCLOSE, &c, 1, NULL, 0, 1000u);
    }
    else esp_finish_now(&w->slot, ESP_FN_TCPCLOSE);
    w->st = W_CLOSE;
}

static void trouble(web_t *w, uint8_t err)
{
    if (!w->err) w->err = err;
    close_then(w, A_FAIL);
}

static void http_line(web_t *w)
{
    char *l = w->line;
    if (!w->status_seen)
    {
        w->status_seen = true;
        const char *sp = strchr(l, ' ');
        w->http = sp ? (uint16_t)atoi(sp + 1) : 0;
        return;
    }
    if (!strncasecmp(l, "Content-Length:", 15)) w->content_len = (uint32_t)strtoul(l + 15, NULL, 10);
    else if (!strncasecmp(l, "Location:", 9))
    {
        const char *v = l + 9;
        while (*v == ' ') v++;
        snprintf(w->location, sizeof w->location, "%s", v);
    }
    else if (!strncasecmp(l, "Set-Cookie:", 11))
    {
        const char *v = l + 11;
        while (*v == ' ') v++;
        keep_cookie(w, v);
    }
}

static void feed(esp_slot_t *s, const uint8_t *p, size_t n)
{
    web_t *w = (web_t *)s;          // (the slot is first)
    size_t i = 0;
    while (i < n && w->st == W_HEAD)
    {
        char c = (char)p[i++];
        if (c == '\r') continue;
        if (c == '\n')
        {
            w->line[w->line_len] = 0;
            if (w->line_len == 0) { w->st = W_BODY; w->body_got = 0; break; }
            http_line(w);
            w->line_len = 0;
            continue;
        }
        if (w->line_len < sizeof w->line - 1) w->line[w->line_len++] = c;
    }
    if (w->st == W_BODY && i < n)
    {
        size_t k = n - i;
        w->body_got += (uint32_t)k;
        bool want = w->http >= 200 && w->http < 300;
        if (want && !w->body_end && w->body_fn && w->body_fn(w, p + i, k)) w->body_end = true;
    }
}

static bool parse_target(web_t *w, const char *u)
{
    const char *p;
    if (!strncmp(u, "http://", 7)) { w->tls = false; p = u + 7; w->port = 80; }
    else if (!strncmp(u, "https://", 8)) { w->tls = true; p = u + 8; w->port = 443; }
    else if (u[0] == '/') { snprintf(w->path, sizeof w->path, "%s", u); return true; }    // the same host
    else return false;
    size_t h = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?')
    {
        if (h >= sizeof w->host - 1) return false;
        w->host[h++] = *p++;
    }
    w->host[h] = 0;
    if (*p == ':') { w->port = (uint16_t)atoi(p + 1); while (*p && *p != '/' && *p != '?') p++; }
    snprintf(w->path, sizeof w->path, "%s%s", *p == '?' ? "/" : "", *p ? p : "/");
    return h > 0;
}

static void connect_now(web_t *w)
{
    w->status_seen = w->body_end = false;
    w->state_asked = 0;
    w->line_len = 0;
    w->location[0] = 0;
    w->content_len = 0;
    w->http = 0;
    w->conn = 0;
    uint8_t f = 0;
    esp_request(&w->slot, ESP_FN_DNS, &f, 1, (const uint8_t *)w->host, strlen(w->host), T_DNS_MS);
    w->t_phase = nethal_millis();
    w->st = W_DNS;
}

static void send_next(web_t *w)
{
    uint8_t a[2] = { w->conn, 1 };
    if (w->req_pos < w->req_len)
    {
        size_t k = w->req_len - w->req_pos;
        if (k > 128u) k = 128u;
        esp_request(&w->slot, ESP_FN_TCPSEND, a, 2, (const uint8_t *)w->req + w->req_pos, k, T_SEND_MS);
        w->req_pos += k;
        return;
    }
    size_t o = w->req_pos - w->req_len, k = w->body_len - o;
    if (k > 128u) k = 128u;
    esp_request(&w->slot, ESP_FN_TCPSEND, a, 2, (const uint8_t *)w->body + o, k, T_SEND_MS);
    w->req_pos += k;
}

static void ask_rcv(web_t *w)
{
    uint8_t a[3] = { w->conn, (uint8_t)RCV_SIZE, (uint8_t)(RCV_SIZE >> 8) };
    esp_request(&w->slot, ESP_FN_TCPRCV, a, 3, NULL, 0, T_RCV_MS);
}

static void end(web_t *w, bool ok)
{
    w->st = W_IDLE;
    esp_clear(&w->slot);
    w->conn = 0;
    if (w->held) { esp_release(); w->held = false; }
    w->result = ok ? WEB_DONE : WEB_FAILED;
    web_done_fn f = w->done_fn;
    w->done_fn = NULL;
    w->body_fn = NULL;
    if (f) f(w, ok);
}

static void complete(web_t *w)
{
    if (w->st != W_BODY) { trouble(w, WEB_E_CLOSED); return; }
    if (w->http >= 300 && w->http < 400 && w->location[0] && w->redirects < MAX_REDIRECTS)
    {
        w->redirects++;
        close_then(w, A_REDIRECT);
        return;
    }
    if (w->http < 200 || w->http >= 300) { trouble(w, WEB_E_HTTP); return; }
    close_then(w, A_DONE);
}

static void make_request(web_t *w)
{
    char host[80];
    if ((w->tls && w->port == 443) || (!w->tls && w->port == 80)) snprintf(host, sizeof host, "%s", w->host);
    else snprintf(host, sizeof host, "%s:%u", w->host, (unsigned)w->port);
    char cookie[sizeof w->cookies + 16] = "";
    if (w->cookies[0]) snprintf(cookie, sizeof cookie, "Cookie: %s\r\n", w->cookies);
    char form[96] = "";
    if (w->body) snprintf(form, sizeof form, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %u\r\n", (unsigned)w->body_len);
    int n = snprintf(w->req, sizeof w->req,
                     "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: " AGENT "\r\nAccept: */*\r\n%s%sConnection: close\r\n\r\n",
                     w->body ? "POST" : "GET", w->path, host, cookie, form);
    w->req_len = (n > 0 && (size_t)n < sizeof w->req) ? (size_t)n : 0;
    w->req_pos = 0;
}

void web_poll(web_t *w)
{
    if (w->st == W_IDLE) return;
    esp_io();
    esp_slot_t *S = &w->slot;
    if (!esp_finished(S) && w->st != W_HEAD && w->st != W_BODY) return;
    if (w->aborting && w->st != W_CLOSE)
    {
        if (S->want_fn && !esp_finished(S)) return;       // its answer first
        w->aborting = false;
        if (w->st == W_OPEN && !S->rsp_timeout && S->rc == 0 && S->pgot >= 1) w->conn = S->pfirst[0];
        esp_clear(S);
        if (!w->err) w->err = WEB_E_TIMEOUT;
        if (w->st == W_DNS) w->conn = 0;
        close_then(w, A_FAIL);
        return;
    }
    uint32_t now = nethal_millis();
    switch (w->st)
    {
        case W_DNS:
            if (S->rsp_timeout || S->rc != 0 || S->pgot < 4) { esp_clear(S); trouble(w, WEB_E_DNS); return; }
            {
                uint8_t a[11];
                memcpy(a, S->pfirst, 4);
                a[4] = (uint8_t)w->port; a[5] = (uint8_t)(w->port >> 8);
                a[6] = 0xFF; a[7] = 0xFF; a[8] = 0; a[9] = 0;
                a[10] = w->tls ? 0x04 : 0x00;
                esp_clear(S);
                esp_request(S, ESP_FN_TCPOPEN, a, 11, w->tls ? (const uint8_t *)w->host : NULL, w->tls ? strlen(w->host) + 1 : 0, T_OPEN_MS);
                w->st = W_OPEN;
            }
            return;
        case W_OPEN:
            if (S->rsp_timeout || S->rc != 0 || S->pgot < 1) { esp_clear(S); trouble(w, WEB_E_CONNECT); return; }
            w->conn = S->pfirst[0];
            esp_clear(S);
            make_request(w);
            send_next(w);
            w->st = W_SEND;
            return;
        case W_SEND:
            if (S->rsp_timeout || S->rc != 0) { esp_clear(S); trouble(w, WEB_E_TIMEOUT); return; }
            esp_clear(S);
            if (w->req_pos < w->req_len + (w->body ? w->body_len : 0)) { send_next(w); return; }
            w->st = W_HEAD;
            w->t_phase = w->wait_until = now;
            ask_rcv(w);
            return;
        case W_HEAD:
        case W_BODY:
            if (S->want_fn == ESP_FN_TCPRCV)
            {
                if (!esp_finished(S)) return;
                bool timeout = S->rsp_timeout;
                uint8_t r = S->rc;
                uint32_t got = S->rcv_data;
                esp_clear(S);
                if (timeout) { trouble(w, WEB_E_TIMEOUT); return; }
                bool ended = w->st == W_BODY && (w->body_end || (w->content_len && w->body_got >= w->content_len) ||
                                                 (w->http >= 300 && w->http < 400 && w->location[0]));
                if (r != 0 || ended) { complete(w); return; }
                w->wait_until = got ? now : now + IDLE_POLL_MS;
                if (got) w->t_phase = now;
            }
            else if (S->want_fn == ESP_FN_TCPSTATE)
            {
                if (!esp_finished(S)) return;
                bool timeout = S->rsp_timeout;
                uint8_t r = S->rc, cstate = S->pfirst[1];
                uint16_t avail = (uint16_t)(S->pfirst[2] | (S->pfirst[3] << 8));
                bool ok = S->pgot >= 4;
                esp_clear(S);
                w->state_asked = now;
                if (!timeout && (r != 0 || (ok && cstate != 4u && avail == 0))) { complete(w); return; }
                w->wait_until = now;
            }
            if (w->hold && w->st == W_BODY && !S->want_fn) { w->t_phase = now; return; }     // (it waits for room)
            if (!S->want_fn && now - w->t_phase >= QUIET_MS && now - w->state_asked >= QUIET_MS)
            {
                uint8_t a = w->conn;
                esp_request(S, ESP_FN_TCPSTATE, &a, 1, NULL, 0, T_RCV_MS);
                return;
            }
            if (now - w->t_phase > T_SILENT_MS || (!w->stream && now - w->t_query > T_QUERY_MS)) { trouble(w, WEB_E_TIMEOUT); return; }
            if (!S->want_fn && (int32_t)(now - w->wait_until) >= 0) ask_rcv(w);
            return;
        case W_CLOSE:
            esp_clear(S);
            w->conn = 0;
            if (w->after == A_REDIRECT)
            {
                char loc[sizeof w->location];
                snprintf(loc, sizeof loc, "%s", w->location);
                if (!parse_target(w, loc)) { w->err = WEB_E_URL; end(w, false); return; }
                // a redirected POST goes on as a GET (as browsers do)
                w->body = NULL;
                w->body_len = 0;
                connect_now(w);
                return;
            }
            end(w, w->after == A_DONE);
            return;
        default:
            return;
    }
}

bool web_init(web_t *w)
{
    memset(w, 0, sizeof *w);
    w->slot.data = feed;
    esp_register(&w->slot);
    return true;
}

bool web_start(web_t *w, const char *url, const char *body, size_t body_len,
               web_body_fn bodyfn, web_done_fn done, bool stream)
{
    w->err = WEB_E_NONE;
    if (w->st != W_IDLE) return false;
    if (!nethal_present()) { w->err = WEB_E_NOWIFI; return false; }
    if (!parse_target(w, url) || url[0] == '/') { w->err = WEB_E_URL; return false; }
    if (!w->held && !esp_acquire()) { w->err = WEB_E_NOWIFI; return false; }
    w->held = true;
    w->aborting = false;
    w->body = body;
    w->body_len = (uint16_t)body_len;
    w->body_fn = bodyfn;
    w->done_fn = done;
    w->stream = stream;
    w->redirects = 0;
    w->result = WEB_BUSY;
    w->t_query = nethal_millis();
    connect_now(w);
    return true;
}

void web_abort(web_t *w)
{
    if (w->st != W_IDLE && w->st != W_CLOSE) w->aborting = true;
}
