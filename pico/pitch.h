// SingStar USB microphone test for PicoVerse 2350
// pitch.h - singing pitch detection (YIN) and level metering
//
// Plain C, unit-tested on a PC (tests/test_pitch.c).

#ifndef PITCH_H
#define PITCH_H

#include <stdint.h>
#include <stdbool.h>

#define PITCH_WINDOW   1024    // input samples analysed per call (93 ms at 11025 Hz)

typedef struct {
    float   dbfs;       // RMS level, dBFS (-99 for silence)
    bool    voiced;     // a clear pitch was found
    float   freq;       // Hz (0 if not voiced)
    int     note;       // MIDI note number (60 = C4), -1 if not voiced
    int     cents;      // -50..+50 from that note
    int     clarity;    // 0..99 (100 * (1 - YIN dip))
} pitch_result_t;

// Analyse PITCH_WINDOW samples taken at sample rate fs (8000..22050 Hz).
void pitch_analyze(const int16_t *x, float fs, pitch_result_t *r);

#endif
