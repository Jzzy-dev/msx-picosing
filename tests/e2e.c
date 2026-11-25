// PicoSing end-to-end test: the real MSX ROM (PICOSING.ROM) runs on a Z80
// core with a fake BIOS and a V9938 model (VRAM, registers, HMMV, palette);
// its cartridge window is served by the real game code, as core 0 and core 1
// of the Pico would. Checks that the MSX's VRAM ends up the same as the
// Pico's picture, measures how fast lists get through, and saves screenshots
// made from the VRAM.
//
// usage: e2e PICOSING.ROM <songs folder> <out folder>

#include "z80.h"
#include "simplat.h"
#include "fake_esp.h"
#include "../pico/game/game.h"
#include "../pico/game/library.h"
#include "../pico/game/dlist.h"
#include "../pico/game/screen_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------------------------------------------------------------- Pico side
static fb_t want;
static uint8_t window[16384];      // the list window at 8000h
static int lists, list_bytes, max_list;
static int stall_every, stall_ms, stall_left;   // a busy Pico (loading a song...)
static bool msx_running_list;                    // between seeing a new seq and asking again
static uint8_t msx_seen_seq;
static int overlaps;                             // a list written while the MSX ran one
static unsigned long wall_ms, req_ms;            // when the MSX last asked for a list
static int pico_latency_max = 8;                 // core 1 finishes what it is doing first (MP3, pitch)

static void pico_tick(void)
{
    if (stall_left > 0) { stall_left--; return; }
    // the real Pico gets to the request 2..8 ms later (an MP3 frame, a pitch analysis)
    if (wall_ms - req_ms < 2 + (req_ms * 2654435761u >> 7) % (unsigned long)(pico_latency_max - 1)) return;
    if (link_service(window))
    {
        if (msx_running_list) overlaps++;
        int n = window[1] | (window[2] << 8);
        lists++; list_bytes += n; if (n > max_list) max_list = n;
    }
}

// ---------------------------------------------------------------- TV beam
// A frame is 'lines' lines of 228 T-states; the VDP interrupt comes after
// the 212 visible rows, so row r is drawn (lines - 212 + r) * 228 T-states
// after it. A list is "torn" when some of its rows were written before the
// beam drew them and others after: the TV then shows half of the change.
static int lines = 313;                // 313 = 50 Hz (Europe), 262 = 60 Hz
static unsigned long vsync_cyc;
static unsigned long cpu_cyc(void);
static int list_ahead, list_behind, torn_lists, measured_lists;
static bool list_open;

static void beam_row_written(int row)
{
    if (!list_open || row < 0 || row >= 212) return;
    unsigned long t = cpu_cyc() - vsync_cyc;
    if (t < (unsigned long)(lines - 212 + row) * 228ul) list_ahead++; else list_behind++;
}

static void beam_list_start(void) { list_open = true; list_ahead = list_behind = 0; }
static void beam_list_end(void)
{
    if (!list_open) return;
    list_open = false;
    if (list_ahead + list_behind == 0) return;
    measured_lists++;
    if (list_ahead && list_behind) torn_lists++;
}

// ---------------------------------------------------------------- V9938
static uint8_t vram[0x20000], reg[64], pal[32], status2;
static uint32_t vaddr;
static bool latch; static uint8_t latch_v;
static int pal_index, pal_half;

static void vdp_reg(int r, uint8_t v)
{
    reg[r] = v;
    if (r == 16) { pal_index = v & 15; pal_half = 0; }
    if (r == 46 && (v & 0xF0) == 0xC0)     // HMMV
    {
        int dx = reg[36] | ((reg[37] & 1) << 8), dy = reg[38] | ((reg[39] & 3) << 8);
        int nx = reg[40] | ((reg[41] & 1) << 8), ny = reg[42] | ((reg[43] & 3) << 8);
        if (!nx) nx = 512;
        if (!ny) ny = 1024;
        for (int y = 0; y < ny; y++) beam_row_written(dy + y);
        for (int y = 0; y < ny; y++)
            for (int x = dx / 2; x < (dx + nx) / 2; x++)
                vram[((dy + y) * 128 + x) & 0x1FFFF] = reg[44];
    }
}

static void vdp_out(uint8_t port, uint8_t v)
{
    switch (port)
    {
        case 0x98: beam_row_written((int)(vaddr / 128)); vram[vaddr] = v; vaddr = (vaddr + 1) & 0x1FFFF; latch = false; break;
        case 0x99:
            if (!latch) { latch_v = v; latch = true; break; }
            latch = false;
            if (v & 0x80) vdp_reg(v & 0x3F, latch_v);
            else vaddr = ((uint32_t)(reg[14] & 7) << 14) | ((v & 0x3F) << 8) | latch_v;
            break;
        case 0x9A:
            pal[pal_index * 2 + pal_half] = v;
            if (++pal_half == 2) { pal_half = 0; pal_index = (pal_index + 1) & 15; }
            break;
        case 0x9B:
        {
            int r = reg[17] & 0x3F;
            vdp_reg(r, v);
            if (!(reg[17] & 0x80)) reg[17] = (uint8_t)((reg[17] & 0xC0) | ((r + 1) & 0x3F));
            break;
        }
    }
}

static uint8_t vdp_in(uint8_t port)
{
    if (port == 0x99) { latch = false; return (reg[15] & 15) == 2 ? status2 : 0x00; }
    return 0xFF;
}

// ---------------------------------------------------------------- MSX
static uint8_t rom[16384], ram[65536];
static uint8_t page2_slot;
static const char *keyq = ""; static int keypos;
static int unknown_bios;

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a >= 0x4000 && a < 0x8000) return rom[a - 0x4000];
    if (a >= 0x8000 && a < 0xC000 && page2_slot == 1)
    {
        if (a == 0x8000 && window[0] != msx_seen_seq) { msx_seen_seq = window[0]; msx_running_list = true; beam_list_start(); }
        return window[a - 0x8000];
    }
    return ram[a];
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x4000 && a < 0x8000)
    {
        if (a == 0x7F80) game_key(v);
        else
        {
            if (a == 0x7F81) { msx_running_list = false; beam_list_end(); req_ms = wall_ms; }
            link_on_write(a, v);
        }
        return;
    }
    if (a >= 0x8000 && a < 0xC000 && page2_slot == 1) return;
    if (a < 0x4000) { printf("write to BIOS %04X\n", a); exit(1); }
    ram[a] = v;
}

static uint8_t pin(z80 *z, uint8_t p) { (void)z; return vdp_in(p); }
static void pout(z80 *z, uint8_t p, uint8_t v) { (void)z; vdp_out(p, v); }
static void ret(z80 *z) { z->pc = ram[z->sp] | (ram[z->sp + 1] << 8); z->sp += 2; }

static void bios(z80 *z)
{
    // roughly what the real routines take
    static const struct { uint16_t a; int t; } cost[] = {
        { 0x009C, 200 }, { 0x009F, 400 }, { 0x00D5, 900 }, { 0x00D8, 500 }, { 0x0047, 150 }, { 0x0024, 600 } };
    for (unsigned i = 0; i < sizeof cost / sizeof cost[0]; i++) if (cost[i].a == z->pc) z->cyc += (unsigned long)cost[i].t;
    switch (z->pc)
    {
        case 0x005F: memset(vram, 0, sizeof vram); break;                 // CHGMOD (5)
        case 0x0062: case 0x0156: break;                                  // CHGCLR, KILBUF
        case 0x0047: vdp_reg(z->c, z->b); break;                          // WRTVDP
        case 0x0138: z->a = 0x04; break;                                  // RSLREG: page 1 = slot 1
        case 0x0024: if (z->h & 0x80) page2_slot = z->a; break;           // ENASLT
        case 0x009C: z->zf = keyq[keypos] == 0; break;                    // CHSNS
        case 0x009F: z->a = (uint8_t)keyq[keypos]; if (keyq[keypos]) keypos++; break;  // CHGET
        case 0x00D5: case 0x00D8: z->a = 0; break;                        // GTSTCK, GTTRIG
        default: unknown_bios++; printf("unknown BIOS call %04X\n", z->pc); break;
    }
    ret(z);
}

static z80 cpu;
static unsigned long cpu_cyc(void) { return cpu.cyc; }
static unsigned long t_states_per_ms = 3580;
static bool freeze_game;      // stop the song clock (to let the screen catch up)

static void run_ms(int ms, int skill)
{
    for (int i = 0; i < ms; i++)
    {
        unsigned long end = cpu.cyc + t_states_per_ms;
        wall_ms++;
        while (cpu.cyc < end)
        {
            if (cpu.pc == 0x38 && !cpu.iff1) cpu.cyc += 3500;   // the BIOS interrupt handler (keyboard scan...)
            if (cpu.pc < 0x4000 && cpu.pc != 0x38 && cpu.pc != 0x39) { bios(&cpu); continue; }
            z80_step(&cpu);
        }
        pico_tick();
        if (stall_every && (now % stall_every) == 0 && !freeze_game) stall_left = stall_ms + (int)((now * 7919u) % 200u);
        if (!freeze_game)
        {
            now++;
            fesp_tick(now);
            if (now % 23 == 0) { sim_sing(0, now, 0); sim_sing(1, now, skill); }
            game_update();
        }
        if (cpu.cyc - vsync_cyc >= (unsigned long)lines * 228ul)
        {
            vsync_cyc += (unsigned long)lines * 228ul;
            z80_gen_int(&cpu, 0xFF);
        }
    }
}

static bool vram_matches(void)
{
    for (int y = 0; y < FB_H; y++)
        if (memcmp(&vram[y * 128], want.px[y], 128)) return false;
    return true;
}

static void save_vram(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n256 212\n255\n");
    for (int y = 0; y < 212; y++)
        for (int x = 0; x < 256; x++)
        {
            uint8_t b = vram[y * 128 + x / 2], c = (x & 1) ? (b & 15) : (b >> 4);
            uint8_t rgb[3] = { (uint8_t)(((pal[c * 2] >> 4) & 7) * 255 / 7), (uint8_t)((pal[c * 2 + 1] & 7) * 255 / 7),
                               (uint8_t)((pal[c * 2] & 7) * 255 / 7) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static int catch_up(const char *what)
{
    freeze_game = true;
    int ms = 0;
    while (ms < 3000)
    {
        run_ms(17, 0);
        ms += 17;
        game_draw(&want);
        if (vram_matches()) break;
    }
    freeze_game = false;
    if (!vram_matches())
    {
        int bad = 0, first = -1;
        for (int y = 0; y < FB_H; y++) if (memcmp(&vram[y * 128], want.px[y], 128)) { bad++; if (first < 0) first = y; }
        printf("%s: %d rows differ, first %d\n", what, bad, first);
        for (int x = 0; x < 128; x++) if (vram[first * 128 + x] != want.px[first][x]) printf("  x%d vram %02x want %02x\n", x * 2, vram[first * 128 + x], want.px[first][x]);
    }
    CHECK(vram_matches(), "%s: VRAM differs from the Pico's picture", what);
    return ms;
}


int main(int argc, char **argv)
{
    if (argc < 4) { printf("usage: e2e ROM songs out\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { printf("no rom\n"); return 2; }
    fclose(f);
    sim_scan(argv[2]);
    static char dd[600];
    snprintf(dd, sizeof dd, "%s/data", argv[3]);
    sim_data = dd;
    char p[700];
    snprintf(p, sizeof p, "%s/SINGERS.TXT", dd); remove(p);
    snprintf(p, sizeof p, "%s/SCORES.TXT", dd); remove(p);
    game_init();
    now = 1000;

    memset(ram, 0xC9, 0x4000);
    ram[0x38] = 0xFB; ram[0x39] = 0xC9;
    ram[0xFAF8] = 0x81;                        // EXBRSA: an MSX2
    z80_init(&cpu);
    cpu.read_byte = rd; cpu.write_byte = wr; cpu.port_in = pin; cpu.port_out = pout;
    cpu.interrupt_mode = 1;
    cpu.sp = 0xF380;
    ram[--cpu.sp] = 0; ram[--cpu.sp] = 0;
    cpu.pc = rom[2] | (rom[3] << 8);

    run_ms(500, 0);
    CHECK(page2_slot == 1, "page 2 mapped to the cartridge (slot %d)", page2_slot);
    int ms = catch_up("title");
    printf("title on screen after %d ms of MSX time (%d lists)\n", 500 + ms, lists);
    save_vram(argv[3], "e2e_title");
    {
        // the title screen: a plasma that flows by palette, a wobbling logo, a scroller
        int lt = lists, bt = list_bytes;
        torn_lists = measured_lists = 0;
        run_ms(4000, 0);
        printf("title: %.1f lists/s, %d bytes/s, %d of %d lists torn\n", (lists - lt) / 4.0,
               (list_bytes - bt) / 4, torn_lists, measured_lists);
        CHECK((lists - lt) / 4.0 > 15, "title keeps up (%.1f lists per second)", (lists - lt) / 4.0);
        save_vram(argv[3], "e2e_title_b");
    }

    keyq = " "; keypos = 0;
    run_ms(150, 0);
    CHECK(game_state() == GS_MAIN, "SPACE: main menu");
    catch_up("main menu");
    CHECK(pal[2 * C_P1] == fb_palette[2 * C_P1] && pal[2 * C_YELLOW + 1] == fb_palette[2 * C_YELLOW + 1], "menu palette loaded");
    save_vram(argv[3], "e2e_main");
    keyq = "\r"; keypos = 0;
    run_ms(150, 0);
    CHECK(game_state() == GS_SONGS, "RETURN: songs");
    catch_up("songs");
    save_vram(argv[3], "e2e_songs");
    keyq = "\r"; keypos = 0;
    run_ms(900, 0);
    CHECK(game_state() == GS_DETAILS, "RETURN: details");
    catch_up("details");
    save_vram(argv[3], "e2e_details");
    keyq = "\r"; keypos = 0;
    run_ms(150, 0);
    CHECK(game_state() == GS_WHO, "RETURN: who sings");
    keyq = "\r"; keypos = 0;
    run_ms(150, 1);
    CHECK(game_state() == GS_PLAY, "RETURN started the song");

    int l0 = lists, b0 = list_bytes;
    torn_lists = measured_lists = 0;
    run_ms(5000, 1);
    printf("50 Hz while singing: %d of %d lists torn (%.0f%%)\n", torn_lists, measured_lists,
           measured_lists ? 100.0 * torn_lists / measured_lists : 0.0);
    int torn50 = torn_lists, meas50 = measured_lists;
    lines = 262; torn_lists = measured_lists = 0;
    run_ms(5000, 1);
    printf("60 Hz while singing: %d of %d lists torn (%.0f%%)\n", torn_lists, measured_lists,
           measured_lists ? 100.0 * torn_lists / measured_lists : 0.0);
    int torn60 = torn_lists, meas60 = measured_lists;
    lines = 313;
    CHECK(meas50 > 100 && torn50 * 20 <= meas50, "50 Hz: at most 5%% of lists torn (%d of %d)", torn50, meas50);
    CHECK(meas60 > 100 && torn60 * 10 <= meas60, "60 Hz: at most 10%% of lists torn (%d of %d)", torn60, meas60);
    printf("while singing: %.1f lists/s, %d bytes/s, largest list %d bytes\n",
           (lists - l0) / 10.0, (list_bytes - b0) / 10, max_list);
    CHECK((lists - l0) / 10.0 > 15, "screen keeps up (%.1f lists per second)", (lists - l0) / 10.0);
    ms = catch_up("singing");
    printf("singing screen complete %d ms after the clock stopped\n", ms);
    save_vram(argv[3], "e2e_singing");

    // A Pico that is busy for 450 ms now and then (longer than the MSX's
    // 0.3 s wait): the old handshake rewrote lists the MSX was still running
    link_strict = false;
    stall_every = 700; stall_ms = 300; overlaps = 0;
    run_ms(12000, 1);
    stall_every = 0;
    int old_overlaps = overlaps;
    int old_bad = 0;
    game_draw(&want);
    int bad_rows = 0;
    for (int y = 0; y < FB_H; y++) if (memcmp(&vram[y * 128], want.px[y], 128)) bad_rows++;
    old_bad = bad_rows;
    printf("old handshake, busy Pico: %d lists written while the MSX ran one, %d rows wrong (lists skipped)\n", old_overlaps, bad_rows);

    link_strict = true;
    catch_up("after the old handshake");          // the row refresh repairs the screen
    stall_every = 700; stall_ms = 300; overlaps = 0;
    int l1 = lists;
    run_ms(12000, 1);
    stall_every = 0;
    freeze_game = true; run_ms(300, 0); freeze_game = false;
    game_draw(&want);
    bad_rows = 0;
    for (int y = 0; y < FB_H; y++) if (memcmp(&vram[y * 128], want.px[y], 128)) bad_rows++;
    printf("new handshake, busy Pico: %d lists written while the MSX ran one, %d rows wrong (%d lists)\n", overlaps, bad_rows, lists - l1);
    CHECK(old_bad > 0, "the busy-Pico test shows the old handshake's lost lists");
    CHECK(overlaps == 0, "no list written while the MSX runs one");
    CHECK(bad_rows == 0, "new handshake keeps the screen right with a busy Pico (%d rows)", bad_rows);

    run_ms(40000, 1);
    CHECK(game_state() == GS_RESULT, "results after the song");
    catch_up("results");
    save_vram(argv[3], "e2e_results");
    printf("scores: blue %d red %d\n", game_score(0), game_score(1));

    keyq = " "; keypos = 0;
    run_ms(100, 0);
    CHECK(game_state() == GS_SONGS, "SPACE back to the songs");
    catch_up("songs again");
    CHECK(unknown_bios == 0, "only known BIOS calls");
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
