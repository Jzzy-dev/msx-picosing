// SingStar USB microphone test for PicoVerse 2350
// uac_desc.h - USB Audio Class 1.0 descriptor parser for microphones
//
// Plain C with no TinyUSB dependency, so it can be unit-tested on a PC.

#ifndef UAC_DESC_H
#define UAC_DESC_H

#include <stdint.h>
#include <stdbool.h>

#define UAC_MAX_RATES 8

typedef struct {
    // AudioControl
    bool     has_ac;
    uint8_t  ac_itf;
    uint8_t  fu_id;          // feature unit, 0 = none
    bool     fu_mute;        // master mute control present
    bool     fu_volume;      // per-channel (or master) volume present
    uint8_t  fu_vol_ch;      // 0 = master volume, 1 = per-channel volume

    // Chosen AudioStreaming alternate setting
    bool     has_stream;
    uint8_t  as_itf;
    uint8_t  alt;
    uint8_t  channels;
    uint8_t  subframe;       // bytes per sample
    uint8_t  bits;
    uint8_t  ep_addr;
    uint8_t  ep_attr;        // bmAttributes
    uint16_t ep_mps;
    uint8_t  ep_interval;
    bool     ep_freq_ctrl;   // endpoint accepts SET_CUR sampling frequency
    bool     rate_continuous;
    uint8_t  n_rates;
    uint32_t rates[UAC_MAX_RATES];   // discrete list, or [min,max] if continuous

    bool     is_midi;        // a MIDIStreaming interface was seen (leave it to a MIDI driver)
} uac_mic_t;

// Walk descriptors (starting at an interface descriptor) and fill in what
// is found. Can be called for the AudioControl interface, the
// AudioStreaming interface, or one range holding both. Returns true when
// something audio related was recognised.
bool uac_parse(const uint8_t *desc, uint16_t len, uac_mic_t *m);

// Does the chosen alt setting support this sample rate?
bool uac_rate_supported(const uac_mic_t *m, uint32_t rate);

#endif
