// UltraStar parser tests: header tags, note types, line breaks, duets,
// #RELATIVE, encodings, timing.
#include "../pico/game/song.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static song_t s;

static const char *txt(const note_t *n) { static char b[64]; memcpy(b, song_note_text(&s, n), n->text_len); b[n->text_len] = 0; return b; }

int main(void)
{
    // 1: a normal song, CRLF, UTF-8 with BOM, decimal comma, all note types
    const char *a =
        "\xEF\xBB\xBF#TITLE:Caf\xC3\xA9 del Mar\r\n#ARTIST:Bj\xC3\xB6rk \xE2\x80\x9CTest\xE2\x80\x9D\r\n#MP3:song.mp3\r\n"
        "#BPM:300,5\r\n#GAP:1500\r\n#START:2.5\r\n#VIDEO:x.mp4\r\n"
        ": 0 4 5 Hel\r\n: 4 2 7 lo \r\n* 8 4 12 gold\r\nF 14 2 0  free\r\nR 18 2 0 rap\r\nG 22 2 0 grap\r\n"
        "- 26\r\n: 30 4 -3  ~\r\n: 34 4 -5 end\r\nE\r\nignored: 99 9 9 x\r\n";
    CHECK(song_parse(a, strlen(a), &s, false), "parse 1");
    CHECK(!strcmp(s.title, "Caf\xE9 del Mar"), "title latin-1 [%s]", s.title);
    CHECK(!strcmp(s.artist, "Bj\xF6rk \"Test\""), "artist [%s]", s.artist);
    CHECK(!strcmp(s.audio, "song.mp3"), "audio");
    CHECK(fabsf(s.bpm - 300.5f) < 0.01f && s.gap_ms == 1500 && s.start_ms == 2500, "bpm %.2f gap %d start %d", s.bpm, s.gap_ms, s.start_ms);
    CHECK(s.n_notes == 8 && s.n_lines == 2 && !s.duet && s.n_tracks == 1, "notes %d lines %d", s.n_notes, s.n_lines);
    CHECK(s.notes[1].len == 2 && s.notes[1].pitch == 7 && !strcmp(txt(&s.notes[1]), "lo "), "note 1 [%s]", txt(&s.notes[1]));
    CHECK(s.notes[2].type == NOTE_GOLDEN && s.notes[3].type == NOTE_FREESTYLE && s.notes[4].type == NOTE_RAP && s.notes[5].type == NOTE_RAP_GOLDEN, "types");
    CHECK(!strcmp(txt(&s.notes[3]), " free"), "leading space kept [%s]", txt(&s.notes[3]));
    CHECK(s.notes[6].pitch == -3 && !strcmp(txt(&s.notes[6]), " "), "~ hidden, its space kept, negative pitch [%s]", txt(&s.notes[6]));
    CHECK(s.lines[0].start == 0 && s.lines[0].end == 24 && s.lines[1].start == 30 && s.lines[1].end == 38, "line ranges");
    CHECK(s.bad_lines == 0, "bad lines %d", s.bad_lines);
    float bm = song_beat_ms(&s);
    CHECK(fabsf(bm - 60000.0f / (300.5f * 4)) < 0.001f, "beat ms");
    CHECK(song_ms_at(&s, 8) == 1500 + (int)(8 * bm), "ms at beat");
    CHECK(fabsf(song_beat_at(&s, 1500 + (int)(10 * bm)) - 10) < 0.05f, "beat at ms");
    CHECK(song_line_at(&s, 0, 5) == 0 && song_line_at(&s, 0, 26) == 1 && song_line_at(&s, 0, 40) == -1, "line at");

    // 2: header only (the song list)
    CHECK(song_parse(a, strlen(a), &s, true) && s.n_notes == 0 && !strcmp(s.audio, "song.mp3"), "header only");

    // 3: duet
    const char *d = "#TITLE:Duet\n#ARTIST:A & B\n#BPM:200\n#GAP:0\nP1\n: 0 2 0 one\n- 4\n: 6 2 2 two\nP2\n: 10 2 4 three\n- 14\n: 16 2 5 four\n: 20 2 5 five\nE\n";
    CHECK(song_parse(d, strlen(d), &s, false), "duet parse");
    CHECK(s.duet && s.n_tracks == 2, "duet flag");
    CHECK(s.tracks[0].first_line == 0 && s.tracks[0].n_lines == 2 && s.tracks[1].first_line == 2 && s.tracks[1].n_lines == 2,
          "tracks %d/%d %d/%d", s.tracks[0].first_line, s.tracks[0].n_lines, s.tracks[1].first_line, s.tracks[1].n_lines);
    CHECK(song_line_at(&s, 1, 0) == 2 && song_line_at(&s, 1, 17) == 3, "duet line at");

    // 4: relative timing
    const char *r = "#TITLE:Rel\n#ARTIST:X\n#BPM:100\n#RELATIVE:yes\n: 0 2 0 a\n: 2 2 0 b\n- 6 8\n: 0 2 0 c\n: 4 2 0 d\n- 8 10\n: 1 1 0 e\nE\n";
    CHECK(song_parse(r, strlen(r), &s, false) && s.relative, "relative parse");
    CHECK(s.notes[2].start == 8 && s.notes[3].start == 12 && s.notes[4].start == 19, "relative beats %d %d %d", s.notes[2].start, s.notes[3].start, s.notes[4].start);

    // 5: Windows-1252 file (not UTF-8)
    const char *w = "#TITLE:Cr\xE8me Br\xFBl\xE9""e\n#ARTIST:\x93Q\x94\n#BPM:100\n: 0 1 0 \xE9t\xE9\nE\n";
    CHECK(song_parse(w, strlen(w), &s, false), "cp1252 parse");
    CHECK(!strcmp(s.title, "Cr\xE8me Br\xFBl\xE9""e") && !strcmp(s.artist, "\"Q\"") && !strcmp(txt(&s.notes[0]), "\xE9t\xE9"), "cp1252 text");

    // 6: '~' at the start of a held syllable ("drea" + "~ms"), curly quotes
    const char *t = "#TITLE:T\n#ARTIST:A\n#BPM:270\n: 0 2 1 drea\n: 3 2 1 ~ms \n: 6 2 1 ~\xE2\x80\x99re \n: 9 2 1 ~ \n: 12 2 1 \xE2\x80\x9CSome\nE\n";
    CHECK(song_parse(t, strlen(t), &s, false), "tilde parse");
    CHECK(!strcmp(txt(&s.notes[1]), "ms ") && !strcmp(txt(&s.notes[2]), "'re ") && !strcmp(txt(&s.notes[3]), " ") && !strcmp(txt(&s.notes[4]), "\"Some"),
          "tilde [%s] [%s] [%s]", txt(&s.notes[1]), txt(&s.notes[2]), txt(&s.notes[4]));

    // 7: not an UltraStar file
    const char *n = "just some text\nwith lines\n";
    CHECK(!song_parse(n, strlen(n), &s, false), "rejects non-UltraStar");

    // text: Latin-1 kept, the rest the nearest Latin-1 letter (the font's), emoji left out
    {
        char o[64];
        static const struct { const char *in, *out; } tx[] = {
            { "Caf\xC3\xA9 M\xC3\xBCnchen", "Caf\xE9 M\xFCnchen" },                           // UTF-8 Latin-1: kept
            { "Pi\xC5\x82sudski \xC5\x81\xC3\xB3""d\xC5\xBA", "Pilsudski L\xF3""dz" },          // Polish
            { "\xC4\x8C""esk\xC3\xA1 \xC5\x99""e\xC4\x8D", "Cesk\xE1 rec" },                    // Czech
            { "Bart\xC3\xB3k \xC5\x91r\xC3\xBCl", "Bart\xF3k or\xFCl" },                        // Hungarian
            { "\xC4\xB0stanbul \xC5\x9F""ark\xC4\xB1", "Istanbul sarki" },                      // Turkish
            { "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", "Moskva" },                  // Cyrillic
            { "\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1", "Atina" },                            // Greek
            { "It\xE2\x80\x99s \xE2\x80\x9Cme\xE2\x80\x9D \xE2\x80\x94 ok\xE2\x80\xA6", "It's \"me\" - ok." },
            { "Love \xF0\x9F\x92\x96 you", "Love  you" },                                     // emoji: left out
            { "Vi\xE1\xBB\x87t Nam", "Viet Nam" },                                            // Vietnamese
            { "\x93Hello\x94 \x96 \x80 \x8A""a", "\"Hello\" - E Sa" },                          // Windows-1252
            { "Ni\xC3\xB1o \xC3\x9F", "Ni\xF1o \xDF" },
        };
        for (size_t k = 0; k < sizeof tx / sizeof tx[0]; k++)
        {
            song_to_latin1(o, sizeof o, tx[k].in, strlen(tx[k].in));
            CHECK(!strcmp(o, tx[k].out), "text %u: [%s] want [%s]", (unsigned)k, o, tx[k].out);
        }
        const char *lyr = "#TITLE:\xD0\x9B\xD1\x8E\xD0\xB1\xD0\xBE\xD0\xB2\xD1\x8C\n#ARTIST:Zespo\xC5\x82\n#BPM:300\n#GAP:0\n: 0 2 5 \xC5\xBC\xC3\xB3\xC5\x82\n: 4 2 5 w\xE2\x80\x99\n- 8\nE\n";
        CHECK(song_parse(lyr, strlen(lyr), &s, false), "parse the Polish/Russian song");
        CHECK(!strcmp(s.title, "Lubov'") && !strcmp(s.artist, "Zespol"), "a song's title and artist [%s] [%s]", s.title, s.artist);
        CHECK(!strcmp(txt(&s.notes[0]), "z\xF3l"), "its syllable [%s]", txt(&s.notes[0]));
        CHECK(!strcmp(txt(&s.notes[1]), "w'"), "its syllable [%s]", txt(&s.notes[1]));
    }

    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
