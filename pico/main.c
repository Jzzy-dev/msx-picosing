// PicoSing - karaoke for the MSX PicoVerse 2350
// main.c - firmware
//
// Core 0: the MSX cartridge bus. Serves
//   4000h-7EFFh  the MSX ROM (msx/picosing.asm)
//   7F00h-7F7Fh  a status block (status.h: the USB microphone details)
//   8000h-BFFFh  the display list window (game/dlist.h)
// and takes the MSX's writes: 7F80h a key, 7F81h "next display list",
// 7F83h A5h "screen cleared". The bus PIO programs and pin setup come from
// Cristiano Goncalves' PicoVerse loadrom firmware (CC BY-NC-SA 4.0).
//
// Core 1: TinyUSB host + SingStar microphones, pitch detection, the
// microSD card, MP3/WAV playback (PicoAmp's decoder), the game, and the
// display lists.
//
// This work is licensed under a "Creative Commons Attribution-NonCommercial-
// ShareAlike 4.0 International License".

#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"
#include "tusb.h"

#include "msx_bus.pio.h"
#include "status.h"
#include "singstar_uac.h"
#include "pitch.h"
#include "game/game.h"
#include "game/dlist.h"
#include "game/screen_link.h"
#include "plat_pico.h"
#include "msx_rom.h"          // generated from msx/picosing.asm: msx_rom[16384]

#define FW_VERSION  0x07

#define PIN_A0      0
#define PIN_A15     15
#define PIN_D0      16
#define PIN_D7      23
#define PIN_RD      24
#define PIN_WR      25
#define PIN_IORQ    26
#define PIN_SLTSL   27
#define PIN_WAIT    28
#define PIN_BUSSDIR 37


// ---------------------------------------------------------------------
// Shared between the cores
// ---------------------------------------------------------------------
volatile uint8_t g_status[ST_WINDOW_SIZE];
static uint8_t rom_ram[16384];
static volatile uint8_t dl_window[16384];

static volatile uint8_t key_ring[32];
static volatile uint32_t key_head, key_tail;

// ---------------------------------------------------------------------
// Core 1
// ---------------------------------------------------------------------
void tuh_mount_cb(uint8_t daddr) { (void)daddr; g_status[ST_DEVICES]++; }
void tuh_umount_cb(uint8_t daddr) { (void)daddr; if (g_status[ST_DEVICES]) g_status[ST_DEVICES]--; }

static void audio_task(void)
{
    static uint32_t last = 0;
    static int16_t win[PITCH_WINDOW];
    const uint32_t hop = 256;     // ~23 ms at 11025 Hz

    uint8_t ch = ss_channels();
    uint32_t w = ss_samples_written();
    if (ch == 0) { last = w; return; }
    if (w < PITCH_WINDOW || (uint32_t)(w - last) < hop) return;
    last = w;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    float fs = (float)ss_sample_rate();
    for (uint8_t c = 0; c < 2; c++)
    {
        pitch_result_t r;
        if (c == 1 && ch == 1)
        {
            memset(&r, 0, sizeof r);
            r.dbfs = -99.0f;
            r.note = -1;
        }
        else
        {
            const int16_t *ring = ss_ring(c);
            uint32_t start = w - PITCH_WINDOW;
            for (uint32_t i = 0; i < PITCH_WINDOW; i++) win[i] = ring[(start + i) & SS_RING_MASK];
            pitch_analyze(win, fs, &r);
        }
        game_mic(c, &r, now);
    }
    g_status[ST_SEQ]++;
}

static void core1_main(void)
{
    plat_init();                // card, song list, audio output
    game_init();
    ss_init(g_status);
    tuh_init(0);

    while (true)
    {
        tuh_task();
        ss_task();

        while (key_tail != key_head)
        {
            uint8_t k = key_ring[key_tail & 31u];
            key_tail++;
            game_key(k);
        }

        plat_audio_pump();
        audio_task();
        plat_audio_pump();
        game_update();

        link_service((uint8_t *)dl_window);
    }
}

static uint32_t core1_stack[16 * 1024 / 4];

// ---------------------------------------------------------------------
// Core 0: MSX bus (after loadrom.c by Cristiano Goncalves)
// ---------------------------------------------------------------------
static PIO bus_pio;
static uint sm_read, sm_write;

static void setup_gpio(void)
{
    for (uint pin = PIN_A0; pin <= PIN_A15; ++pin)
    {
        gpio_init(pin);
        gpio_set_input_hysteresis_enabled(pin, true);
        gpio_set_dir(pin, GPIO_IN);
    }
    for (uint pin = PIN_D0; pin <= PIN_D7; ++pin)
    {
        gpio_init(pin);
        gpio_set_input_hysteresis_enabled(pin, true);
    }
    gpio_init(PIN_RD);      gpio_set_dir(PIN_RD, GPIO_IN);
    gpio_init(PIN_WR);      gpio_set_dir(PIN_WR, GPIO_IN);
    gpio_init(PIN_IORQ);    gpio_set_dir(PIN_IORQ, GPIO_IN);
    gpio_init(PIN_SLTSL);   gpio_set_dir(PIN_SLTSL, GPIO_IN);
    gpio_init(PIN_BUSSDIR); gpio_set_dir(PIN_BUSSDIR, GPIO_IN);
}

static void bus_init(void)
{
    bus_pio = pio0;
    sm_read = 0;
    sm_write = 1;
    pio_sm_claim(bus_pio, sm_read);
    pio_sm_claim(bus_pio, sm_write);
    uint off_read  = pio_add_program(bus_pio, &msx_read_responder_program);
    uint off_write = pio_add_program(bus_pio, &msx_write_captor_program);

    pio_sm_config cr = msx_read_responder_program_get_default_config(off_read);
    sm_config_set_in_pins(&cr, PIN_A0);
    sm_config_set_in_shift(&cr, false, false, 16);
    sm_config_set_out_pins(&cr, PIN_D0, 8);
    sm_config_set_out_shift(&cr, true, false, 32);
    sm_config_set_sideset_pins(&cr, PIN_WAIT);
    sm_config_set_jmp_pin(&cr, PIN_RD);
    sm_config_set_clkdiv(&cr, 1.0f);
    pio_sm_init(bus_pio, sm_read, off_read, &cr);

    pio_sm_config cw = msx_write_captor_program_get_default_config(off_write);
    sm_config_set_in_pins(&cw, PIN_A0);
    sm_config_set_in_shift(&cw, false, false, 32);
    sm_config_set_fifo_join(&cw, PIO_FIFO_JOIN_RX);
    sm_config_set_jmp_pin(&cw, PIN_WR);
    sm_config_set_clkdiv(&cw, 1.0f);
    pio_sm_init(bus_pio, sm_write, off_write, &cw);

    pio_gpio_init(bus_pio, PIN_WAIT);
    pio_sm_set_consecutive_pindirs(bus_pio, sm_read, PIN_WAIT, 1, true);
    for (uint pin = PIN_D0; pin <= PIN_D7; ++pin) pio_gpio_init(bus_pio, pin);
    pio_sm_set_consecutive_pindirs(bus_pio, sm_read, PIN_D0, 8, false);
    pio_sm_set_consecutive_pindirs(bus_pio, sm_write, PIN_D0, 8, false);
    gpio_put(PIN_WAIT, 1);

    pio_sm_set_enabled(bus_pio, sm_read, true);
    pio_sm_set_enabled(bus_pio, sm_write, true);
}

static inline void __not_in_flash_func(drain_writes)(void)
{
    while (!pio_sm_is_rx_fifo_empty(bus_pio, sm_write))
    {
        uint32_t s = pio_sm_get(bus_pio, sm_write);
        uint16_t addr = (uint16_t)(s & 0xFFFFu);
        uint8_t data = (uint8_t)(s >> 16);
        if (addr == 0x7F80u)
        {
            uint32_t h = key_head;
            if (h - key_tail < 32u)
            {
                key_ring[h & 31u] = data;
                key_head = h + 1u;
            }
        }
        else
        {
            link_on_write(addr, data);     // 7F81h next list, 7F83h hello
        }
    }
}

static void __no_inline_not_in_flash_func(bus_loop)(void)
{
    while (true)
    {
        uint16_t addr;
        while (true)
        {
            drain_writes();
            if (!pio_sm_is_rx_fifo_empty(bus_pio, sm_read))
            {
                addr = (uint16_t)pio_sm_get(bus_pio, sm_read);
                break;
            }
        }
        // writes that came in with this read go first (a key, a request)
        drain_writes();

        uint8_t data = 0xFFu;
        bool drive = false;
        if (addr >= 0x4000u && addr <= 0x7FFFu)
        {
            drive = true;
            data = ((addr & 0xFF80u) == ST_WINDOW_BASE) ? g_status[addr & 0x7Fu] : rom_ram[addr - 0x4000u];
        }
        else if (addr >= 0x8000u && addr <= 0xBFFFu)
        {
            drive = true;
            data = dl_window[addr - 0x8000u];
        }
        pio_sm_put_blocking(bus_pio, sm_read, (uint32_t)data | ((drive ? 0xFFu : 0x00u) << 8));
    }
}

int main(void)
{
    // Same flash timing and clock as the PicoVerse loadrom firmware
    qmi_hw->m[0].timing = 0x40000202;
    set_sys_clock_khz(210000, true);

    setup_gpio();

    memcpy(rom_ram, msx_rom, sizeof rom_ram);
    memset((void *)g_status, 0, sizeof g_status);
    memset((void *)dl_window, 0, sizeof dl_window);
    dl_window[DL_HEADER] = DL_OP_END;
    g_status[ST_MAGIC0] = 'S';
    g_status[ST_MAGIC1] = 'S';
    g_status[ST_VERSION] = FW_VERSION;

    // The bus first: the MSX may already look for the ROM. Core 1 adds the
    // I2S program to the same PIO, so it starts only once the bus is set up.
    bus_init();
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof core1_stack);
    bus_loop();
    return 0;
}
