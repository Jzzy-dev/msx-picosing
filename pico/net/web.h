// PicoSing - karaoke for the MSX PicoVerse 2350
// web.h - HTTP(S) requests over the ESP-01, several at once (one each per
// web_t): a GET or a form POST, the body handed over as it comes in,
// redirects followed, cookies kept.

#ifndef PICOSING_WEB_H
#define PICOSING_WEB_H

#include "net_int.h"

#define WEB_IDLE    0
#define WEB_BUSY    1
#define WEB_DONE    2
#define WEB_FAILED  3

// why a request failed
#define WEB_E_NONE     0
#define WEB_E_NOWIFI   1
#define WEB_E_DNS      2
#define WEB_E_CONNECT  3
#define WEB_E_TIMEOUT  4
#define WEB_E_HTTP     5      // web_http() says which
#define WEB_E_URL      6
#define WEB_E_CLOSED   7

typedef struct web web_t;
typedef bool (*web_body_fn)(web_t *w, const uint8_t *p, size_t n);   // true: enough
typedef void (*web_done_fn)(web_t *w, bool ok);

struct web
{
    esp_slot_t  slot;           // first: the ESP's slot is the request's
    uint8_t     st, conn, redirects, after, err, result;
    bool        held, aborting, tls, status_seen, body_end, stream;
    uint16_t    http, port;
    char        host[64];
    char        path[424];
    char        req[900];
    size_t      req_len, req_pos;
    const char *body;           // a POST's form (the caller's until done)
    uint16_t    body_len;
    char        location[300];
    char        line[240];
    size_t      line_len;
    uint32_t    content_len, body_got;
    uint32_t    t_query, t_phase, wait_until, state_asked;
    web_body_fn body_fn;
    web_done_fn done_fn;
    void       *user;
    char        cookies[240];   // "a=1; b=2": sent along, Set-Cookie added
};

bool web_init(web_t *w);        // once; false: no WiFi link
// GET (body NULL) or POST url (a form, application/x-www-form-urlencoded).
// stream: no time limit (a reply that goes on). false: busy, or no WiFi.
bool web_start(web_t *w, const char *url, const char *body, size_t body_len,
               web_body_fn bodyfn, web_done_fn done, bool stream);
void web_poll(web_t *w);        // often
void web_abort(web_t *w);       // ends it (done comes, not ok)
bool web_busy(const web_t *w);
uint16_t web_http(const web_t *w);
uint8_t  web_error(const web_t *w);
const char *web_error_text(const web_t *w);

// "a b&c" -> "a+b%26c" (for a form or a query); false when it does not fit
bool web_escape(char *out, size_t size, const char *s);

#endif
