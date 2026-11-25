// PicoSing - karaoke for the MSX PicoVerse 2350
// music.c - the song's music: the card's MP3 (platform), or a player on the
// PC (pclink.c). The singing screen asks this one for the time.

#include "music.h"
#include "platform.h"
#include "settings.h"
#include "pclink.h"
#include <stdio.h>
#include <string.h>

static bool on_pc;

bool music_on_pc(void) { return on_pc; }

bool music_start(int i, const song_t *s, char *err, int n)
{
    // the PC's player when chosen, or when there is no MP3 and a PC is set
    char dir[200], p[300];
    bool have_mp3 = false;
    if (s->audio[0] && plat_song_dir(i, dir, sizeof dir))
    {
        snprintf(p, sizeof p, "%s/%s", dir, s->audio);
        have_mp3 = plat_exists(p);
    }
    on_pc = g_set.music_pc || (!have_mp3 && g_set.pc[0]);
    if (on_pc)
    {
        if (!pc_start(s)) { snprintf(err, (size_t)n, "%s", pc_message()); return false; }
        if (g_set.volume >= 0) pc_set_volume(g_set.volume);
        return true;
    }
    if (!have_mp3)
    {
        snprintf(err, (size_t)n, s->youtube[0] ? "NO MP3: SET A PC PLAYER" : "THE MP3 IS MISSING");
        return false;
    }
    return plat_audio_start(i, s, err, n);
}

int32_t music_ms(void)
{
    if (!on_pc) return plat_audio_ms();
    int st = pc_state();
    if (st == PC_FAILED || st == PC_OFF) return -1;
    if (st != PC_PLAYING) return MUSIC_WAITING;
    int32_t t = pc_ms();
    return t < 0 ? MUSIC_WAITING : t;
}

bool music_waiting(void) { return on_pc && (pc_state() == PC_CONNECTING || pc_state() == PC_WAITING); }
void music_go_now(void) { if (on_pc) pc_go_now(); }

void music_stop(void)
{
    if (on_pc) pc_stop();
    else plat_audio_stop();
}

void music_pause(bool p)
{
    if (on_pc) pc_pause(p);
    else plat_audio_pause(p);
}

void music_seek(int32_t ms)
{
    if (on_pc) pc_seek(ms);
    else plat_audio_seek(ms);
}

void music_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g_set.volume = v;
    if (on_pc) pc_set_volume(v);
    plat_set_volume(v);
}

const char *music_message(void) { return on_pc ? pc_message() : ""; }
