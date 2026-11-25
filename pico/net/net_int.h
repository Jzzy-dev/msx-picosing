// PicoSing - karaoke for the MSX PicoVerse 2350
// net_int.h - the ESP-01 link (esp.c, from PicoAmp) and the HTTP client
// (web.c) on top of it.
//
// The ESP-01 runs the UNAPI ESP8266 firmware (the PicoVerse's WiFi): it
// keeps its own WiFi settings, set up from the MSX side as usual.

#ifndef PICOSING_NET_INT_H
#define PICOSING_NET_INT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// what the platform gives (plat_pico.c on the cartridge, a fake ESP in tests)
uint32_t nethal_millis(void);
bool     nethal_present(void);
bool     nethal_open(void);                            // false: no ESP link
void     nethal_close(void);
size_t   nethal_write(const uint8_t *p, size_t n);     // non-blocking, bytes taken
size_t   nethal_read(uint8_t *p, size_t n);            // non-blocking, bytes given

// the UNAPI functions
#define ESP_FN_NETSTATE     3u
#define ESP_FN_TCPOPEN      13u
#define ESP_FN_TCPCLOSE     14u
#define ESP_FN_TCPABORT     15u
#define ESP_FN_TCPSTATE     16u
#define ESP_FN_TCPSEND      17u
#define ESP_FN_TCPRCV       18u
#define ESP_FN_DNS          206u
#define ESP_FN_UDPOPEN      8u
#define ESP_FN_UDPCLOSE     9u
#define ESP_FN_UDPSEND      11u
#define ESP_FN_UDPRCV       12u

typedef struct esp_slot
{
    uint8_t  txq[320];
    uint16_t txq_len;
    uint8_t  want_fn;           // 0 = nothing asked
    bool     queued;            // waits for the wire
    uint32_t timeout_ms;
    bool     rsp_done, rsp_timeout;
    uint8_t  rc;
    uint16_t pgot;              // payload bytes received
    uint8_t  pfirst[8];         // first payload bytes (ip, conn, state)
    uint32_t rcv_data;          // TCP_RCV data bytes (given to 'data')
    void   (*data)(struct esp_slot *s, const uint8_t *p, size_t n);
} esp_slot_t;

void esp_register(esp_slot_t *s);
bool esp_acquire(void);
void esp_release(void);
bool esp_link_open(void);
void esp_request(esp_slot_t *s, uint8_t fn, const uint8_t *a, size_t n, const uint8_t *b, size_t m, uint32_t timeout);
void esp_finish_now(esp_slot_t *s, uint8_t fn);
bool esp_finished(const esp_slot_t *s);
void esp_clear(esp_slot_t *s);
void esp_shorten(esp_slot_t *s, uint32_t ms);
void esp_io(void);
extern uint32_t esp_timeouts, esp_drained;

#endif
