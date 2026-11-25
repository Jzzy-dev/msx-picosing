// SingStar USB microphone test for PicoVerse 2350
// singstar_uac.h - USB Audio Class 1.0 microphone host driver (TinyUSB app driver)
//
// Works with the PlayStation SingStar USB dongle (Nam Tai, 1415:0000) and
// other UAC1 USB microphones with a 16-bit isochronous IN stream.
// Needs the TinyUSB patch patches/tinyusb-rp2-host-iso.patch.

#ifndef SINGSTAR_UAC_H
#define SINGSTAR_UAC_H

#include <stdint.h>
#include <stdbool.h>

#define SS_RING_SIZE  4096u            // samples per channel, power of two
#define SS_RING_MASK  (SS_RING_SIZE - 1u)

// Commands (also the bytes the MSX writes to 7F80h)
#define SS_CMD_RESTART   'R'
#define SS_CMD_GAIN_UP   '+'
#define SS_CMD_GAIN_DOWN '-'
#define SS_CMD_PID       'P'
#define SS_CMD_EPTYPE    'T'
#define SS_CMD_LAST      'L'
#define SS_CMD_RATE      'F'
#define SS_CMD_CLEAR     '0'

void ss_init(volatile uint8_t *status);   // before tuh_init()
void ss_task(void);                       // call often, after tuh_task()
void ss_command(uint8_t cmd);             // from the USB core only

// Audio: one ring per channel. Channel 0 = left (first mic), 1 = right.
uint32_t       ss_samples_written(void);  // total samples per channel so far
const int16_t *ss_ring(uint8_t ch);
uint32_t       ss_sample_rate(void);
uint8_t        ss_channels(void);         // 0 when not streaming

// Status helpers
static inline void st_put16(volatile uint8_t *st, uint8_t off, uint16_t v)
{
    st[off] = (uint8_t)v; st[off + 1] = (uint8_t)(v >> 8);
}
static inline void st_put32(volatile uint8_t *st, uint8_t off, uint32_t v)
{
    st[off] = (uint8_t)v; st[off + 1] = (uint8_t)(v >> 8);
    st[off + 2] = (uint8_t)(v >> 16); st[off + 3] = (uint8_t)(v >> 24);
}

#endif
