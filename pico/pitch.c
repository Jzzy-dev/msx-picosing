// SingStar USB microphone test for PicoVerse 2350
// pitch.c - singing pitch detection (YIN) and level metering
//
// YIN runs on the full-rate window: at 11025 Hz that is about 60k
// multiply-adds per mic per analysis, well under a millisecond on the
// RP2350's FPU, so both mics can be analysed every ~23 ms.

#include "pitch.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define YIN_W        512                  // integration window (46 ms at 11025 Hz)
#define F_MIN        60.0f
#define F_MAX        1400.0f
#define YIN_THRESH   0.15f                // a dip below this is always good enough
#define YIN_ACCEPT   0.35f                // no dip below this: not voiced
#define YIN_SUBDIP   0.25f                // octave check: dip at 1/2 or 1/3 period
#define SILENCE_DB   -50.0f               // below this: no pitch

void pitch_analyze(const int16_t *x, float fs, pitch_result_t *r)
{
    static float y[PITCH_WINDOW];
    static float d[PITCH_WINDOW];

    memset(r, 0, sizeof(*r));
    r->note = -1;

    // --- DC removal + level ---
    int32_t sum = 0;
    for (int i = 0; i < PITCH_WINDOW; i++) sum += x[i];
    float mean = (float)sum / PITCH_WINDOW;

    float energy = 0.0f;
    for (int i = 0; i < PITCH_WINDOW; i++)
    {
        float v = (float)x[i] - mean;
        energy += v * v;
    }
    float rms = sqrtf(energy / PITCH_WINDOW);
    r->dbfs = (rms < 1.0f) ? -99.0f : 20.0f * log10f(rms / 32768.0f);

    if (r->dbfs < SILENCE_DB) return;

    for (int i = 0; i < PITCH_WINDOW; i++) y[i] = (float)x[i] - mean;
    float fsd = fs;

    int tau_min = (int)(fsd / F_MAX);
    int tau_max = (int)(fsd / F_MIN) + 1;
    if (tau_min < 2) tau_min = 2;
    if (tau_max > PITCH_WINDOW - YIN_W - 1) tau_max = PITCH_WINDOW - YIN_W - 1;

    // --- YIN difference function + cumulative mean normalisation ---
    d[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= tau_max; tau++)
    {
        float acc = 0.0f;
        const float *a = y;
        const float *b = y + tau;
        for (int j = 0; j < YIN_W; j++)
        {
            float diff = a[j] - b[j];
            acc += diff * diff;
        }
        running += acc;
        d[tau] = (running > 0.0f) ? acc * (float)tau / running : 1.0f;
    }

    // --- pick the dip ---
    // Classic YIN takes the first dip under a fixed threshold. With vibrato
    // or breathy singing no dip may get that low, and the deepest dip is
    // then often a multiple of the period (octave too low). So the
    // threshold adapts to the deepest dip and the first (shortest-period)
    // dip close to it still wins.
    float gmin = 1.0f;
    for (int tau = tau_min; tau <= tau_max; tau++)
        if (d[tau] < gmin) gmin = d[tau];
    if (gmin >= YIN_ACCEPT) return;

    float thresh = gmin * 1.5f + 0.05f;
    if (thresh < YIN_THRESH) thresh = YIN_THRESH;
    if (thresh > YIN_ACCEPT) thresh = YIN_ACCEPT;

    int best = -1;
    for (int tau = tau_min; tau <= tau_max; tau++)
    {
        if (d[tau] < thresh)
        {
            while (tau + 1 <= tau_max && d[tau + 1] < d[tau]) tau++;
            best = tau;
            break;
        }
    }
    if (best < 0) return;

    // --- octave check ---
    // A high note's period falls between two whole sample lags, so its dip
    // looks shallower than the dip at two or three periods. If a good dip
    // sits at 1/2 or 1/3 of the chosen lag, that is the real period.
    for (int div = 3; div >= 2; div--)
    {
        int c = (best + div / 2) / div;
        int pick = -1;
        for (int k = c - 1; k <= c + 1; k++)
        {
            if (k < tau_min || k >= best) continue;
            if (pick < 0 || d[k] < d[pick]) pick = k;
        }
        if (pick >= 0 && d[pick] < YIN_SUBDIP && abs(pick * div - best) <= div)
        {
            best = pick;
            break;
        }
    }

    // --- parabolic interpolation ---
    float t = (float)best;
    if (best > 1 && best < tau_max)
    {
        float s0 = d[best - 1], s1 = d[best], s2 = d[best + 1];
        float den = s0 - 2.0f * s1 + s2;
        if (den > 1e-9f) t += 0.5f * (s0 - s2) / den;
    }

    float f = fsd / t;
    if (f < F_MIN || f > F_MAX) return;

    float dip = d[best];
    if (dip < 0.0f) dip = 0.0f;
    int clarity = (int)((1.0f - dip) * 100.0f);
    if (clarity > 99) clarity = 99;
    if (clarity < 0) clarity = 0;

    float midi = 69.0f + 12.0f * log2f(f / 440.0f);
    int note = (int)lrintf(midi);
    r->voiced  = true;
    r->freq    = f;
    r->note    = note;
    r->cents   = (int)lrintf((midi - (float)note) * 100.0f);
    r->clarity = clarity;
}
