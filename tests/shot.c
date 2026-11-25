// Screenshot of a song at a time: shot <songs folder> <song number> <ms> <out.ppm>
// (simulated singers in tune, for the pitch lines)
#include "../pico/game/game.h"
#include "../pico/game/library.h"
#include "../pico/game/fb.h"
#include "simplat.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    sim_scan(argv[1]);
    game_init();
    now = 1000;
    for (int i = 0; i < atoi(argv[2]); i++) game_key(KEY_DOWN);
    game_key(KEY_RETURN);
    uint32_t end = now + (uint32_t)atoi(argv[3]);
    while (now < end) { now++; if (now % 23 == 0) sim_sing(0, now, 0); game_update(); }
    static fb_t f;
    game_draw(&f);
    FILE *o = fopen(argv[4], "wb");
    fprintf(o, "P6\n256 212\n255\n");
    for (int y = 0; y < 212; y++)
        for (int x = 0; x < 256; x++)
        {
            uint8_t b = f.px[y][x / 2], c = (x & 1) ? (b & 15) : (b >> 4);
            uint8_t rgb[3] = { (uint8_t)(((fb_palette[c * 2] >> 4) & 7) * 255 / 7), (uint8_t)((fb_palette[c * 2 + 1] & 7) * 255 / 7), (uint8_t)((fb_palette[c * 2] & 7) * 255 / 7) };
            fwrite(rgb, 1, 3, o);
        }
    fclose(o);
    printf("%s - %s at %s ms\n", game_song()->artist, game_song()->title, argv[3]);
    return 0;
}
