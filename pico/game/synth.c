// PicoSing - karaoke for the MSX PicoVerse 2350
// synth.c - the title tune (see synth.h). Composed for PicoSing.

#include "synth.h"
#include <math.h>
#include <string.h>

#define BPM        132
#define STEPS      128          // 8 bars of 16 sixteenths

// Lead: one entry per eighth note (MIDI note, 0 = rest, -1 = hold)
static const int8_t lead[64] = {
    72, 76, 79, 84,  83, 79, 76, 79,     // C
    81, -1, 76, 81,  84, 83, 81, 76,     // Am
    77, 81, 84, 81,  77, 81, 84, 86,     // F
    86, -1, 83, 79,  86, -1, 79, 83,     // G
    88, 86, 84, 79,  76, 79, 84, 88,     // C
    86, 84, 81, 76,  72, 76, 81, 84,     // Am
    89, 88, 86, 84,  81, 84, 86, 89,     // F
    88, 86, 83, 79,  86, -1, 84, -1,     // G
};
// One chord per bar: root (MIDI) and the chord's notes above C4
static const int8_t bass_root[8] = { 36, 45, 41, 43, 36, 45, 41, 43 };
static const int8_t chords[8][3] = {
    { 60, 64, 67 }, { 57, 60, 64 }, { 57, 60, 65 }, { 59, 62, 67 },
    { 60, 64, 67 }, { 57, 60, 64 }, { 57, 60, 65 }, { 59, 62, 67 },
};

static uint32_t frame;          // frames since reset (at the rate last used)
static uint32_t step_count;
static float ph_lead, ph_bass, ph_ch[3], ph_kick;
static float lead_freq, lead_env;
static uint32_t noise = 0x12345678u;

void synth_reset(void)
{
    frame = 0;
    step_count = 0;
    ph_lead = ph_bass = ph_kick = 0;
    memset(ph_ch, 0, sizeof ph_ch);
    lead_freq = 0;
    lead_env = 0;
}

uint32_t synth_step(void) { return step_count; }

static float hz(int midi) { return 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f); }

static float white(void)
{
    noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
    return (float)(int32_t)noise / 2147483648.0f;
}

void synth_render(int16_t *out, uint32_t frames, uint32_t rate)
{
    const float step_len = (float)rate * 60.0f / (BPM * 4.0f);    // frames per 1/16
    const float inv = 1.0f / (float)rate;

    for (uint32_t i = 0; i < frames; i++, frame++)
    {
        float pos = (float)frame / step_len;
        uint32_t step = (uint32_t)pos;
        float in_step = pos - (float)step;                // 0..1 within the sixteenth
        step_count = step;
        int s = (int)(step % STEPS);
        int bar = s / 16;
        int eighth = s / 2;

        // lead: a new note on every eighth that is not a hold
        int note = lead[eighth];
        if (note > 0 && (s % 2) == 0 && in_step * step_len < 1.0f) { lead_freq = hz(note); lead_env = 1.0f; }
        if (note == 0) lead_env *= 0.995f;
        float t_note = ((float)(s % 2) + in_step) * step_len * inv;   // seconds into the eighth
        float vib = 1.0f + 0.006f * sinf(2.0f * 3.14159f * 6.0f * ((float)frame * inv)) * (t_note > 0.08f ? 1.0f : 0.0f);
        ph_lead += lead_freq * vib * inv;
        if (ph_lead >= 1.0f) ph_lead -= 1.0f;
        float lead_v = (ph_lead < 0.25f ? 1.0f : -0.33f) * lead_env;
        lead_env *= 0.99993f;

        // bass: root and octave on eighths, triangle
        float bf = hz(bass_root[bar] + ((eighth % 2) ? 12 : 0));
        ph_bass += bf * inv;
        if (ph_bass >= 1.0f) ph_bass -= 1.0f;
        float tri = ph_bass < 0.5f ? ph_bass * 4.0f - 1.0f : 3.0f - ph_bass * 4.0f;
        float bass_env = 1.0f - 0.6f * ((float)(s % 2) + in_step) / 2.0f;
        float bass_v = tri * bass_env;

        // chords: soft squares on the off-beat eighths
        float ch_v = 0;
        if ((eighth % 2) == 1)
        {
            float env = 1.0f - ((float)(s % 2) + in_step) / 2.0f;
            for (int k = 0; k < 3; k++)
            {
                ph_ch[k] += hz(chords[bar][k]) * inv;
                if (ph_ch[k] >= 1.0f) ph_ch[k] -= 1.0f;
                ch_v += (ph_ch[k] < 0.5f ? 1.0f : -1.0f) * env;
            }
        }

        // drums: kick on 1 and 3, snare on 2 and 4, hi-hat on the eighths
        float dr = 0;
        int beat16 = s % 16;
        float t16 = in_step * step_len * inv;               // seconds into the sixteenth
        if (beat16 == 0 || beat16 == 8)
        {
            float f = 150.0f * expf(-t16 * 30.0f) + 45.0f;
            ph_kick += f * inv;
            dr += sinf(2.0f * 3.14159f * ph_kick) * expf(-t16 * 18.0f) * 0.9f;
        }
        else ph_kick = 0;
        if (beat16 == 4 || beat16 == 12) dr += white() * expf(-t16 * 22.0f) * 0.45f;
        if ((s % 2) == 0) dr += white() * expf(-t16 * 90.0f) * 0.18f;

        float l = lead_v * 0.26f + bass_v * 0.30f + ch_v * 0.045f + dr * 0.35f;
        float r = lead_v * 0.22f + bass_v * 0.30f + ch_v * 0.055f + dr * 0.35f;
        if (l > 1.0f) l = 1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f;
        if (r < -1.0f) r = -1.0f;
        out[i * 2] = (int16_t)(l * 24000.0f);
        out[i * 2 + 1] = (int16_t)(r * 24000.0f);
    }
}
