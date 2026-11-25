// PicoSing - karaoke for the MSX PicoVerse 2350
// settings.c - SETTINGS.TXT: KEY=value lines
//
// The USDB password is kept as it is typed: the card is the player's own.

#include "settings.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

settings_t g_set = { false, "", "", "", 80, 60, 0 };

static void value(const char *text, const char *key, char *out, size_t size)
{
    char k[24];
    snprintf(k, sizeof k, "\n%s=", key);
    // (the first line has no line end before it)
    const char *p = !strncmp(text, k + 1, strlen(k + 1)) ? text + strlen(k + 1) : NULL;
    if (!p) { p = strstr(text, k); if (p) p += strlen(k); }
    if (!p) return;
    size_t n = strcspn(p, "\r\n");
    if (n >= size) n = size - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

void settings_fix_pc(void)
{
    char a[sizeof g_set.pc];
    const char *p = g_set.pc;
    while (*p == ' ') p++;
    if (!strncasecmp(p, "http://", 7)) p += 7;
    else if (!strncasecmp(p, "https://", 8)) p += 8;
    size_t n = 0;
    for (; *p && *p != '/' && *p != ' ' && *p != '?' && n + 1 < sizeof a; p++)
        a[n++] = (char)tolower((unsigned char)*p);
    a[n] = 0;
    if (n && !strchr(a, ':') && n + 6 < sizeof a) strcat(a, ":8765");
    if (n && a[n - 1] == ':' && n + 5 < sizeof a) strcat(a, "8765");
    snprintf(g_set.pc, sizeof g_set.pc, "%s", a);
}

void settings_load(void)
{
    static char text[1024];
    char path[80], v[64];
    size_t len;
    snprintf(path, sizeof path, "%s/SETTINGS.TXT", plat_data_dir());
    if (!plat_read_file(path, text, sizeof text, &len)) return;
    v[0] = 0; value(text, "MUSIC", v, sizeof v);
    if (v[0]) g_set.music_pc = !strcmp(v, "PC");
    value(text, "PC", g_set.pc, sizeof g_set.pc);
    settings_fix_pc();
    value(text, "USDB_USER", g_set.usdb_user, sizeof g_set.usdb_user);
    value(text, "USDB_PASS", g_set.usdb_pass, sizeof g_set.usdb_pass);
    v[0] = 0; value(text, "VOLUME", v, sizeof v);
    if (v[0]) g_set.volume = atoi(v);
    v[0] = 0; value(text, "MIC_DELAY", v, sizeof v);
    if (v[0]) g_set.mic_delay = atoi(v);
    v[0] = 0; value(text, "PC_SYNC", v, sizeof v);
    if (v[0]) g_set.pc_sync = atoi(v);
    if (g_set.volume < 0 || g_set.volume > 100) g_set.volume = 80;
}

void settings_save(void)
{
    char text[600], path[80];
    int n = snprintf(text, sizeof text,
                     "ROOT=%s\nMUSIC=%s\nPC=%s\nUSDB_USER=%s\nUSDB_PASS=%s\nVOLUME=%d\nMIC_DELAY=%d\nPC_SYNC=%d\n",
                     plat_song_root(), g_set.music_pc ? "PC" : "CARD", g_set.pc, g_set.usdb_user, g_set.usdb_pass,
                     g_set.volume, g_set.mic_delay, g_set.pc_sync);
    snprintf(path, sizeof path, "%s/SETTINGS.TXT", plat_data_dir());
    if (n > 0) plat_write_file(path, text, (size_t)n);
}
