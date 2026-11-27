// PicoSing - karaoke for the MSX PicoVerse 2350
// plat_pico.c - the game's platform on the cartridge: the microSD card
// (FatFs), the song list, and the music (PicoAmp's decoder + I2S)
//
// Songs: one folder per song under /KARAOKE (or /SONGS) on the card's first
// partition, with the UltraStar .txt and its audio file (MP3 or WAV).
//
// The song clock: the music starts when the first decoded buffer goes to
// the I2S output; from then on the clock is the time since, held back to
// what was decoded (so a slow card never runs the notes ahead of the music).

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "ff.h"
#include "game/platform.h"
#include "game/library.h"
#include "game/game.h"
#include "game/synth.h"
#include "pva/pva_internal.h"
#include "audio/audio_hal.h"
#include "plat_pico.h"
#include "net/net_int.h"
#include "game/pcfs.h"

#define TXT_MAX     (48 * 1024)
#define HEAD_MAX    (6 * 1024)

static FATFS fs;
static const char *problem = "NO MICROSD CARD";
static char txt[TXT_MAX + 1];
static bool audio_ok;

// playback
static bool playing, ended;
static bool music_want, music_on;
static int32_t synth_gain = 32768;     // the title tune's volume (plat_set_volume)      // the title tune
static char data_dir[32] = "0:/KARAOKE/PICOSING";
static uint32_t end_at;
static int32_t offset_ms;
static bool paused;
static int32_t paused_at;
static uint32_t hold_until;      // after a jump: let the old sound drain first

uint32_t plat_millis(void) { return to_ms_since_boot(get_absolute_time()); }
const char *plat_card_problem(void) { return problem; }

static bool read_file(const char *path, char *buf, size_t max, size_t *len)
{
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return false;
    UINT br = 0;
    FRESULT fr = f_read(&f, buf, (UINT)max, &br);
    f_close(&f);
    if (fr != FR_OK) return false;
    buf[br] = 0;
    *len = br;
    return true;
}

static char song_root[128], card_root[128] = "0:/KARAOKE";
const char *plat_card_root(void) { return card_root; }

static void add_song(const char *folder_path, const char *folder, const char *name)
{
    char path[300];
    snprintf(path, sizeof path, "%s/%s", folder_path, name);
    size_t len;
    if (!read_file(path, txt, HEAD_MAX, &len)) return;
    if (!strstr(txt, "#BPM") && !strstr(txt, "#bpm")) return;     // not an UltraStar song
    song_t *s = game_song();
    if (!song_parse(txt, len, s, true)) return;
    bool duet = strstr(txt, "\nP2") || strstr(txt, "\nP 2") || strstr(txt, "#P1:") || strstr(txt, "#DUETSINGERP1");
    char title[64];
    if (s->title[0]) snprintf(title, sizeof title, "%s", s->title);
    else song_to_latin1(title, sizeof title, folder, strlen(folder));
    lib_add(s->artist[0] ? s->artist : "?", title, duet, path);
}

// The songs in dir and its folders, 4 levels deep (a song: a .txt in a
// folder). PicoSing's own folder is skipped.
static int scan_dir(const char *dir, const char *name, int depth)
{
    DIR d;
    FILINFO fi;
    int found = 0;
    if (f_opendir(&d, dir) != FR_OK) return -1;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0])
    {
        if (fi.fname[0] == '.' || (fi.fattrib & (AM_HID | AM_SYS))) continue;
        if (!(fi.fattrib & AM_DIR))
        {
            size_t n = strlen(fi.fname);
            if (depth > 0 && n > 4 && !strcasecmp(fi.fname + n - 4, ".txt"))
            {
                int before = lib_count();
                add_song(dir, name, fi.fname);
                found += lib_count() - before;
            }
            continue;
        }
        if (!strcasecmp(fi.fname, "PICOSING") || depth >= 4) continue;
        char sub[300], nm[80];
        snprintf(sub, sizeof sub, "%s/%s", dir, fi.fname);
        snprintf(nm, sizeof nm, "%s", fi.fname);
        int r = scan_dir(sub, nm, depth + 1);
        if (r > 0) found += r;
    }
    f_closedir(&d);
    return found;
}

static int rescan(void)
{
    if (pcfs_is(song_root)) return pcfs_scan(song_root);
    lib_clear();
    int n = scan_dir(song_root, "", 0);
    lib_sort();
    return n;
}

const char *plat_song_root(void) { return song_root; }

bool plat_set_song_root(const char *path)
{
    if (pcfs_is(path))
    {
        // the PC's folder: its songs from PicoAmp for Windows; no answer: the folder before stays
        int n = pcfs_scan(path);
        if (n < 0) return false;          // (the list before stays)
        snprintf(song_root, sizeof song_root, "%s", path);
        problem = NULL;
        return true;
    }
    snprintf(song_root, sizeof song_root, "%s", path);
    int n = rescan();
    problem = n < 0 ? "CANNOT READ THE FOLDER" : NULL;
    if (n >= 0) snprintf(card_root, sizeof card_root, "%s", path);
    return n >= 0;
}

// the PC's folder, chosen last time: after the settings (its address), at the start
static bool pc_root_due;
void plat_late_init(void)
{
    if (!pc_root_due) return;
    pc_root_due = false;
    char r[sizeof song_root];
    snprintf(r, sizeof r, "%s", song_root);
    const char *dflt = strstr(data_dir, "/SONGS/") ? "0:/SONGS" : "0:/KARAOKE";
    snprintf(song_root, sizeof song_root, "%s", dflt);       // (the card's, when the PC does not answer)
    if (pcfs_scan(r) >= 0) snprintf(song_root, sizeof song_root, "%s", r);
    else { rescan(); problem = NULL; }
}

void plat_idle(void) { tight_loop_contents(); }

int plat_list_dir(const char *path, plat_dirent_t *out, int max)
{
    if (pcfs_is(path)) return pcfs_list(path, out, max);
    DIR d;
    FILINFO fi;
    int n = 0;
    if (f_opendir(&d, path) != FR_OK) return -1;
    while (n < max && f_readdir(&d, &fi) == FR_OK && fi.fname[0])
    {
        if (fi.fname[0] == '.' || (fi.fattrib & (AM_HID | AM_SYS))) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", fi.fname);
        out[n].dir = (fi.fattrib & AM_DIR) != 0;
        out[n].size = (uint32_t)fi.fsize;
        n++;
    }
    f_closedir(&d);
    return n;
}

bool plat_remove(const char *path) { return !pcfs_is(path) && f_unlink(path) == FR_OK; }
bool plat_rename(const char *from, const char *to) { return !pcfs_is(from) && f_rename(from, to) == FR_OK; }

// open files for the tag editor: two at a time
static FIL ff[2];
static bool ff_open[2];
int plat_fopen(const char *path, bool write)
{
    if (pcfs_is(path)) return -1;
    for (int h = 0; h < 2; h++)
        if (!ff_open[h])
        {
            BYTE m = write ? (FA_READ | FA_WRITE | FA_OPEN_ALWAYS) : FA_READ;
            if (f_open(&ff[h], path, m) != FR_OK) return -1;
            ff_open[h] = true;
            return h;
        }
    return -1;
}
size_t plat_fread(int h, void *buf, size_t n) { UINT br = 0; if (f_read(&ff[h], buf, (UINT)n, &br) != FR_OK) return 0; return br; }
size_t plat_fwrite(int h, const void *buf, size_t n) { UINT bw = 0; if (f_write(&ff[h], buf, (UINT)n, &bw) != FR_OK) return 0; return bw; }
bool plat_fseek(int h, uint32_t off) { return f_lseek(&ff[h], off) == FR_OK; }
uint32_t plat_fsize(int h) { return (uint32_t)f_size(&ff[h]); }
bool plat_ftruncate(int h) { return f_truncate(&ff[h]) == FR_OK; }
void plat_fclose(int h) { if (h >= 0 && h < 2 && ff_open[h]) { f_close(&ff[h]); ff_open[h] = false; } }

void plat_init(void)
{
    audio_ok = pva_dec_init() && pva_hal_audio_init();
    pva_dec_set_volume(90);

    lib_clear();
    if (f_mount(&fs, "0:", 1) != FR_OK) { problem = "NO MICROSD CARD"; return; }
    // the data folder: in /KARAOKE (or /SONGS), else on the card's root
    static FILINFO fi;
    if (f_stat("0:/KARAOKE", &fi) == FR_OK) snprintf(data_dir, sizeof data_dir, "0:/KARAOKE/PICOSING");
    else if (f_stat("0:/SONGS", &fi) == FR_OK) snprintf(data_dir, sizeof data_dir, "0:/SONGS/PICOSING");
    else snprintf(data_dir, sizeof data_dir, "0:/PICOSING");
    f_mkdir(data_dir);
    // the song folder: the one chosen last time, else /KARAOKE or /SONGS
    const char *dflt = strstr(data_dir, "/SONGS/") ? "0:/SONGS" : "0:/KARAOKE";
    snprintf(song_root, sizeof song_root, "%s", dflt);
    snprintf(card_root, sizeof card_root, "%s", dflt);
    char p[64];
    snprintf(p, sizeof p, "%s/SETTINGS.TXT", data_dir);
    size_t len;
    if (read_file(p, txt, 512, &len))
    {
        char *r = strstr(txt, "ROOT=");
        if (r)
        {
            r += 5;
            size_t k = strcspn(r, "\r\n");
            if (k && k < sizeof song_root) { memcpy(song_root, r, k); song_root[k] = 0; }
        }
    }
    if (pcfs_is(song_root)) { pc_root_due = true; problem = NULL; return; }   // (plat_late_init)
    int n = rescan();
    if (n < 0 && strcmp(song_root, dflt))      // the chosen folder is gone
    {
        snprintf(song_root, sizeof song_root, "%s", dflt);
        n = rescan();
    }
    problem = (n < 0) ? "NO /KARAOKE FOLDER" : NULL;
    if (n >= 0) snprintf(card_root, sizeof card_root, "%s", song_root);
}

bool plat_song_load(int i, song_t *s, char *err, int n)
{
    size_t len;
    const char *path = lib_path(i);
    if (path && pcfs_is(path))
    {
        if (!pcfs_read(path, txt, TXT_MAX, &len)) { snprintf(err, (size_t)n, "%s", pcfs_error()); return false; }
    }
    else if (!path || !read_file(path, txt, TXT_MAX, &len)) { snprintf(err, (size_t)n, "CANNOT READ THE SONG"); return false; }
    if (!song_parse(txt, len, s, false)) { snprintf(err, (size_t)n, "NOT AN ULTRASTAR SONG"); return false; }
    return true;
}

static void audio_path(int i, const song_t *s, char *path, size_t size)
{
    const char *txt_path = lib_path(i);
    const char *slash = txt_path ? strrchr(txt_path, '/') : NULL;
    if (!slash) { path[0] = 0; return; }
    snprintf(path, size, "%.*s/%s", (int)(slash - txt_path), txt_path, s->audio);
}

bool plat_audio_start(int i, const song_t *s, char *err, int n)
{
    if (!audio_ok) { snprintf(err, (size_t)n, "NO AUDIO OUTPUT"); return false; }
    music_on = false;
    char path[300];
    audio_path(i, s, path, sizeof path);
    pva_track_info_t info;
    int r = pva_dec_open(path, &info);
    if (r != PVA_OK)
    {
        snprintf(err, (size_t)n, r == PVA_E_NOTFOUND ? "AUDIO FILE NOT FOUND" :
                                 r == PVA_E_FORMAT ? "AUDIO: MP3 OR WAV ONLY" : "CANNOT READ THE AUDIO");
        return false;
    }
    offset_ms = 0;
    if (s->start_ms > 0 && pva_dec_seek((uint32_t)(s->start_ms / 1000)) == PVA_OK)
        offset_ms = (s->start_ms / 1000) * 1000;
    audio_hal_mark_start();
    pva_hal_audio_mute(false);
    playing = true;
    ended = false;
    return true;
}

void plat_audio_stop(void)
{
    paused = false;
    hold_until = 0;
    pva_dec_close();
    pva_hal_audio_mute(true);
    playing = false;
    ended = false;
}

void plat_audio_pause(bool on)
{
    if (!playing || on == paused) return;
    if (on)
    {
        paused_at = plat_audio_ms();
        paused = true;
        pva_hal_audio_mute(true);    // what's queued plays out silently
    }
    else
    {
        // go on where the decoder is: the queue is empty by now
        paused = false;
        offset_ms = (int32_t)pva_dec_elapsed_ms();
        audio_hal_mark_start();
        pva_hal_audio_mute(false);
    }
}

void plat_audio_seek(int32_t ms)
{
    if (!playing) return;
    paused = false;
    if (ms < 0) ms = 0;
    if (pva_dec_seek((uint32_t)(ms / 1000)) == PVA_OK) offset_ms = (ms / 1000) * 1000;
    else offset_ms = (int32_t)pva_dec_elapsed_ms();   // can't seek: go on
    pva_hal_audio_mute(true);
    hold_until = plat_millis() + 160;    // the I2S queue empties, then the new sound
    audio_hal_mark_start();
}

int32_t plat_audio_ms(void)
{
    if (paused) return paused_at;
    if (hold_until) return offset_ms;
    if (ended && (int32_t)(plat_millis() - end_at) >= 0) return -1;
    if (!playing && !ended) return -1;
    uint32_t t0 = audio_hal_first_commit_ms();
    if (!t0) return offset_ms;
    int32_t t = (int32_t)(plat_millis() - t0);
    int32_t decoded = (int32_t)pva_dec_elapsed_ms() - offset_ms;
    if (playing && t > decoded) t = decoded;
    return offset_ms + t;
}

bool plat_audio_preview(int i, const song_t *s, int32_t from_ms)
{
    if (pcfs_is(lib_path(i))) return false;              // (a PC's song: its music plays there, when sung)
    if (!audio_ok || !s->audio[0]) return false;
    music_on = false;
    char path[300];
    audio_path(i, s, path, sizeof path);
    pva_track_info_t info;
    if (pva_dec_open(path, &info) != PVA_OK) return false;
    offset_ms = 0;
    if (from_ms > 0 && pva_dec_seek((uint32_t)(from_ms / 1000)) == PVA_OK) offset_ms = (from_ms / 1000) * 1000;
    audio_hal_mark_start();
    pva_hal_audio_mute(false);
    playing = true;
    ended = false;
    return true;
}

bool plat_song_tags(int i, const song_t *s, song_tags_t *t)
{
    memset(t, 0, sizeof *t);
    if (!audio_ok || !s->audio[0] || playing || pcfs_is(lib_path(i))) return false;    // (a PC's song: its MP3 stays there)
    char path[300];
    audio_path(i, s, path, sizeof path);
    static pva_track_info_t info;
    if (pva_dec_open(path, &info) != PVA_OK) return false;
    pva_dec_close();
    t->ok = true;
    snprintf(t->title, sizeof t->title, "%s", info.title);
    snprintf(t->artist, sizeof t->artist, "%s", info.artist);
    snprintf(t->album, sizeof t->album, "%s", info.album);
    t->seconds = info.total_seconds;
    t->kbps = info.bitrate_kbps;
    t->rate = info.sample_rate;
    t->pic_pos = info.pic_pos;
    t->pic_len = info.pic_len;
    return true;
}

void plat_music(bool on) { music_want = on; }

// ---------------------------------------------------------------- files
const char *plat_data_dir(void) { return data_dir; }

bool plat_read_file(const char *path, char *buf, size_t max, size_t *len)
{
    return read_file(path, buf, max, len);
}

static bool put(const char *path, BYTE mode, const char *d, size_t n)
{
    FIL f;
    if (f_open(&f, path, mode) != FR_OK) return false;
    UINT bw = 0;
    FRESULT fr = f_write(&f, d, (UINT)n, &bw);
    f_close(&f);
    return fr == FR_OK && bw == n;
}
bool plat_write_file(const char *p, const char *d, size_t n) { return put(p, FA_WRITE | FA_CREATE_ALWAYS, d, n); }
bool plat_append_file(const char *p, const char *d, size_t n) { return put(p, FA_WRITE | FA_OPEN_APPEND, d, n); }

bool plat_exists(const char *path)
{
    if (pcfs_is(path)) return pcfs_exists(path);
    static FILINFO fi;
    return f_stat(path, &fi) == FR_OK;
}

bool plat_song_dir(int i, char *dir, size_t size)
{
    const char *p = lib_path(i);
    const char *sl = p ? strrchr(p, '/') : NULL;
    if (!sl) return false;
    snprintf(dir, size, "%.*s", (int)(sl - p), p);
    return true;
}

static FIL sf;
static bool sf_open;
static uint32_t sf_left;
static bool sf_pc;
bool plat_stream_open(const char *path, uint32_t off, uint32_t len)
{
    plat_stream_close();
    sf_pc = pcfs_is(path);
    if (sf_pc) return pcfs_stream_open(path, off, len);
    if (sf_open) f_close(&sf);
    sf_open = f_open(&sf, path, FA_READ) == FR_OK;
    if (!sf_open) return false;
    if (off && f_lseek(&sf, off) != FR_OK) { f_close(&sf); sf_open = false; return false; }
    sf_left = len ? len : 0xFFFFFFFFu;
    return true;
}
size_t plat_stream_read(uint8_t *buf, size_t n)
{
    if (sf_pc) return pcfs_stream_read(buf, n);
    if (!sf_open) return 0;
    if (n > sf_left) n = sf_left;
    UINT br = 0;
    if (f_read(&sf, buf, (UINT)n, &br) != FR_OK) return 0;
    sf_left -= br;
    return br;
}
void plat_stream_close(void) { if (sf_pc) { pcfs_stream_close(); sf_pc = false; } if (sf_open) f_close(&sf); sf_open = false; }

// ---------------------------------------------------------------- pump
static void music_pump(void)
{
    if (music_want && !music_on && !playing)
    {
        synth_reset();
        music_on = true;
        pva_hal_audio_mute(false);
    }
    else if (!music_want && music_on)
    {
        music_on = false;
        if (!playing) pva_hal_audio_mute(true);
    }
    if (!music_on) return;
    for (int i = 0; i < 2; i++)
    {
        uint32_t max;
        int16_t *b = pva_hal_audio_acquire(&max);
        if (!b) return;
        if (max > 512) max = 512;
        synth_render(b, max, 22050);
        if (synth_gain != 32768)
            for (uint32_t k = 0; k < max * 2; k++) b[k] = (int16_t)((b[k] * synth_gain) >> 15);
        pva_hal_audio_commit(max, 22050);
    }
}

void plat_audio_pump(void)
{
    if (!playing) { if (audio_ok) music_pump(); return; }
    if (paused) return;
    if (hold_until)
    {
        if ((int32_t)(plat_millis() - hold_until) < 0) return;
        hold_until = 0;
        pva_hal_audio_mute(false);
    }
    for (int i = 0; i < 3; i++)
    {
        int r = pva_dec_step();
        if (r < 0)
        {
            playing = false;
            ended = true;
            end_at = plat_millis() + 200;      // let the queued audio play out
            return;
        }
        if (r == 0) return;
    }
}

// ---------------------------------------------------------------- volume, folders, new songs
void plat_set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    pva_dec_set_volume((uint8_t)v);
    int32_t g = 32768;
    for (int i = v; i < 100; i++) g = (g * 30935) >> 15;
    synth_gain = v ? g : 0;
}

bool plat_mkdir(const char *path)
{
    // each folder of the path, from the top
    char p[260];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = strchr(p + 3, '/'); ; s = strchr(s + 1, '/'))
    {
        if (s) *s = 0;
        FRESULT r = f_mkdir(p);
        if (r != FR_OK && r != FR_EXIST) return false;
        if (!s) return true;
        *s = '/';
    }
}

bool plat_add_song(const char *txt_path)
{
    const char *sl = strrchr(txt_path, '/');
    if (!sl) return false;
    char dir[260], folder[80];
    snprintf(dir, sizeof dir, "%.*s", (int)(sl - txt_path), txt_path);
    const char *fs = strrchr(dir, '/');
    snprintf(folder, sizeof folder, "%s", fs ? fs + 1 : dir);
    int before = lib_count();
    add_song(dir, folder, sl + 1);
    lib_sort();
    return lib_count() > before;
}

// ---------------------------------------------------------------- the ESP-01 (WiFi)
// UART1 on GPIO 38 (TX) / 39 (RX), as the PicoVerse firmware has it, at the
// UNAPI ESP8266 firmware's 859372 baud; what comes in goes to a ring (IRQ).
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#define ESP_UART      uart1
#define ESP_TX_PIN    38
#define ESP_RX_PIN    39
#define ESP_BAUD      859372u
#define ESP_RX_SIZE   4096u
static uint8_t esp_rx[ESP_RX_SIZE];
static volatile uint32_t esp_head, esp_tail;
static bool esp_ready;

static void __not_in_flash_func(esp_rx_irq)(void)
{
    while (uart_is_readable(ESP_UART))
    {
        uint8_t b = (uint8_t)uart_getc(ESP_UART);
        uint32_t h = esp_head, n = (h + 1u) % ESP_RX_SIZE;
        if (n == esp_tail) continue;              // full: lost
        esp_rx[h] = b;
        __dmb();
        esp_head = n;
    }
}

uint32_t nethal_millis(void) { return to_ms_since_boot(get_absolute_time()); }
bool nethal_present(void) { return true; }

bool nethal_open(void)
{
    if (!esp_ready)
    {
        uart_init(ESP_UART, ESP_BAUD);
        gpio_set_function(ESP_TX_PIN, GPIO_FUNC_UART_AUX);
        gpio_set_function(ESP_RX_PIN, GPIO_FUNC_UART_AUX);
        uart_set_format(ESP_UART, 8, 1, UART_PARITY_NONE);
        uart_set_hw_flow(ESP_UART, false, false);
        uart_set_fifo_enabled(ESP_UART, true);
        while (uart_is_readable(ESP_UART)) (void)uart_getc(ESP_UART);
        irq_set_exclusive_handler(UART1_IRQ, esp_rx_irq);
        irq_set_enabled(UART1_IRQ, true);
        uart_set_irq_enables(ESP_UART, true, false);
        esp_ready = true;
    }
    esp_tail = esp_head;
    return true;
}

void nethal_close(void) { }

size_t nethal_write(const uint8_t *p, size_t n)
{
    size_t done = 0;
    while (done < n && uart_is_writable(ESP_UART)) uart_putc_raw(ESP_UART, (char)p[done++]);
    return done;
}

size_t nethal_read(uint8_t *p, size_t n)
{
    size_t done = 0;
    uint32_t t = esp_tail, h = esp_head;
    __dmb();
    while (done < n && t != h) { p[done++] = esp_rx[t]; t = (t + 1u) % ESP_RX_SIZE; }
    __dmb();
    esp_tail = t;
    return done;
}
