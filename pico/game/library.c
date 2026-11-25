// PicoSing - karaoke for the MSX PicoVerse 2350
// library.c - the song list

#include "library.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static lib_entry_t entries[LIB_MAX];
static int count;
static char pool[LIB_POOL];
static uint16_t pool_used;

void lib_clear(void) { count = 0; pool_used = 0; }

static void copy(char *d, size_t n, const char *s)
{
    size_t i = 0;
    for (; s && s[i] && i + 1 < n; i++) d[i] = s[i];
    d[i] = 0;
}

bool lib_add(const char *artist, const char *title, bool duet, const char *path)
{
    size_t pl = strlen(path) + 1;
    if (count >= LIB_MAX || pool_used + pl > LIB_POOL) return false;
    lib_entry_t *e = &entries[count++];
    copy(e->artist, sizeof e->artist, artist);
    copy(e->title, sizeof e->title, title);
    e->duet = duet ? 1 : 0;
    e->path = pool_used;
    memcpy(pool + pool_used, path, pl);
    pool_used += (uint16_t)pl;
    return true;
}

static int ci_cmp(const char *a, const char *b)
{
    for (;; a++, b++)
    {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
}

static int cmp(const void *pa, const void *pb)
{
    const lib_entry_t *a = pa, *b = pb;
    int c = ci_cmp(a->artist, b->artist);
    return c ? c : ci_cmp(a->title, b->title);
}

void lib_sort(void) { qsort(entries, (size_t)count, sizeof entries[0], cmp); }
int lib_count(void) { return count; }
void lib_remove(int i)
{
    if (i < 0 || i >= count) return;
    memmove(&entries[i], &entries[i + 1], (size_t)(count - i - 1) * sizeof entries[0]);
    count--;
}
const lib_entry_t *lib_get(int i) { return (i >= 0 && i < count) ? &entries[i] : NULL; }
const char *lib_path(int i) { return (i >= 0 && i < count) ? pool + entries[i].path : NULL; }
