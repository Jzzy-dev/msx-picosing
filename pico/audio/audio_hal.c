// PicoSing - karaoke for the MSX PicoVerse 2350
// audio_hal.c - what PicoAmp's decoder needs: an arena, the I2S output and
// stubs for the engine parts PicoSing has no use for.
//
// The I2S part is PicoAmp's pva_hal_pico.c (Jzzy): pico_audio_i2s
// (pico-extras) on PIO0 SM2 with the PicoVerse 2350 DAC pins, buffers from
// the arena. Runs on core 1 (the DMA IRQ belongs to the calling core).
//
// This work is licensed under a "Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
// License". https://creativecommons.org/licenses/by-nc-sa/4.0/

#include "pico/stdlib.h"
#include "pico/audio_i2s.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/util/buffer.h"
#include "hardware/sync.h"
#include <string.h>
#include "../pva/pva_internal.h"
#include "audio_hal.h"

#define I2S_DATA_PIN  29
#define I2S_BCLK_PIN  30     // LRCLK = 31
#define I2S_MUTE_PIN  32     // high = muted
#define I2S_BUFFERS   5      // ~130 ms of 44.1 kHz audio queued
#define I2S_FRAMES    1152

// ---------------------------------------------------------------- arena
#define ARENA_SIZE (72u * 1024u)
static uint8_t arena[ARENA_SIZE] __attribute__((aligned(8)));
static size_t arena_used;

void *pva_alloc(size_t size)
{
    size = (size + 7u) & ~(size_t)7u;
    if (arena_used + size > ARENA_SIZE) return NULL;
    void *p = arena + arena_used;
    arena_used += size;
    memset(p, 0, size);
    return p;
}

void pva_log(const char *fmt, ...) { (void)fmt; }
size_t pva_net_read(uint8_t *dst, size_t n) { (void)dst; (void)n; return 0; }
bool pva_net_ended(void) { return true; }
void pva_eq_reset(void) {}
void pva_eq_process(int16_t *s, uint32_t f, uint32_t r) { (void)s; (void)f; (void)r; }
void pva_viz_feed(const int16_t *s, uint32_t f, uint32_t r) { (void)s; (void)f; (void)r; }
uint32_t pva_hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

// ---------------------------------------------------------------- I2S
static audio_format_t audio_format = {
    .sample_freq = 44100,
    .format = AUDIO_BUFFER_FORMAT_PCM_S16,
    .channel_count = 2,
};

static struct audio_buffer_format producer_format = {
    .format = &audio_format,
    .sample_stride = 4,
};

static struct audio_buffer_pool *pool;
static struct audio_buffer *current;
static volatile uint32_t first_commit_ms;
static volatile bool armed;

static struct audio_buffer_pool *new_arena_pool(void)
{
    audio_buffer_pool_t *ac = pva_alloc(sizeof(audio_buffer_pool_t));
    audio_buffer_t *bufs = pva_alloc(I2S_BUFFERS * sizeof(audio_buffer_t));
    mem_buffer_t *mems = pva_alloc(I2S_BUFFERS * sizeof(mem_buffer_t));
    uint8_t *bytes = pva_alloc(I2S_BUFFERS * I2S_FRAMES * 4u);
    if (!ac || !bufs || !mems || !bytes) return NULL;
    for (int i = 0; i < I2S_BUFFERS; i++)
    {
        mems[i].size = I2S_FRAMES * 4u;
        mems[i].bytes = bytes + (size_t)i * I2S_FRAMES * 4u;
        mems[i].flags = 0;
        bufs[i].format = &producer_format;
        bufs[i].buffer = &mems[i];
        bufs[i].max_sample_count = I2S_FRAMES;
        bufs[i].sample_count = 0;
        bufs[i].next = (i != I2S_BUFFERS - 1) ? &bufs[i + 1] : NULL;
    }
    ac->type = ac_producer;
    ac->format = producer_format.format;
    ac->free_list_spin_lock = spin_lock_init(SPINLOCK_ID_AUDIO_FREE_LIST_LOCK);
    ac->free_list = bufs;
    ac->prepared_list_spin_lock = spin_lock_init(SPINLOCK_ID_AUDIO_PREPARED_LISTS_LOCK);
    ac->prepared_list = NULL;
    ac->prepared_list_tail = NULL;
    ac->connection = NULL;
    return ac;
}

bool pva_hal_audio_init(void)
{
    gpio_init(I2S_MUTE_PIN);
    gpio_set_dir(I2S_MUTE_PIN, GPIO_OUT);
    gpio_put(I2S_MUTE_PIN, 1);

    pool = new_arena_pool();
    if (!pool) return false;

    int dma = dma_claim_unused_channel(false);
    if (dma < 0) return false;
    dma_channel_unclaim((uint)dma);

    struct audio_i2s_config cfg = {
        .data_pin = I2S_DATA_PIN,
        .clock_pin_base = I2S_BCLK_PIN,
        .dma_channel = (uint8_t)dma,
        .pio_sm = 2,
    };
    if (!audio_i2s_setup(&audio_format, &cfg)) return false;
    if (!audio_i2s_connect(pool)) return false;
    audio_i2s_set_enabled(true);
    return true;
}

void pva_hal_audio_mute(bool mute) { gpio_put(I2S_MUTE_PIN, mute ? 1 : 0); }

int16_t *pva_hal_audio_acquire(uint32_t *max_frames)
{
    if (!pool) return NULL;
    if (!current) current = take_audio_buffer(pool, false);
    if (!current) return NULL;
    *max_frames = current->max_sample_count;
    return (int16_t *)current->buffer->bytes;
}

void pva_hal_audio_commit(uint32_t frames, uint32_t sample_rate)
{
    if (!current) return;
    if (sample_rate && audio_format.sample_freq != sample_rate) audio_format.sample_freq = sample_rate;
    current->sample_count = frames;
    give_audio_buffer(pool, current);
    current = NULL;
    if (armed)
    {
        armed = false;
        first_commit_ms = pva_hal_millis();
    }
}

void audio_hal_mark_start(void) { armed = true; first_commit_ms = 0; }
uint32_t audio_hal_first_commit_ms(void) { return armed ? 0 : first_commit_ms; }
