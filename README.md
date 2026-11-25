# PicoSing - karaoke for the MSX PicoVerse 2350

SingStar-style karaoke on an MSX2 with a PicoVerse 2350 cartridge and the
PlayStation SingStar USB microphones. The cartridge's RP2350 does
everything: it reads the songs from the microSD card, plays the music,
listens to both mics, scores the singing and draws the screen. The MSX2
shows the picture and passes on the keys.

**Version 0.7: online.** PicoSing uses the PicoVerse's WiFi (the ESP-01, with the WiFi settings
it already has).
- **FIND SONGS (USDB):** search [USDB](https://usdb.animux.de) from the MSX by artist and/or
  title, and save a song with RETURN.
  - USDB has the notes and words (`.txt`) of UltraStar songs, but no music.
  - You need a (free) USDB account. Type its name and password under SETTINGS; they're stored in
    `SETTINGS.TXT` on the card, as plain text.
  - A song goes to `<song folder>/USDB/<Artist> - <Title>/`, with its USDB cover, and shows up
    in the song list at once. RETURN on a saved song (marked OK) opens it.
  - PicoSing asks for the song's text the way USDB Syncer does, without the website's 25-second
    countdown.
  - If the WiFi can't reach USDB, PicoAmp for Windows fetches the pages for the MSX (see below).
    It uses the MSX's login, or its own (`[usdb]` in `picoamp.ini`) when the MSX has none.
- **The PC as the music player:** with PicoAmp for Windows' MSX link on (its **K** key), PicoSing
  can play a song's music on the PC.
  - With a YouTube link in `#VIDEO` (as USDB songs have), the PC opens that video in the
    browser.
  - Without one, the PC opens a Spotify search for the song (the app, else the web player); you
    pick the track there.
  - PicoSing follows the player's time from the PC, about every half second, and moves the notes
    and lyrics with it. `#VIDEOGAP` is taken into account.
  - When to use it:
    - Songs without an MP3 always play this way, if a PC is set in SETTINGS.
    - SETTINGS > MUSIC: PC PLAYER plays every song this way.
  - SPACE pauses the PC's player too, and N/P make it jump.
  - **,** and **.** move the PC's sound 50 ms earlier or later against the notes. The setting is
    kept.
  - If the player doesn't start by itself, start it on the PC. SPACE tells PicoSing it plays.
  - A YouTube ad before the video confuses the timing until the video starts; PicoSing catches
    up then.
- **Volume:** up/down while singing, or SETTINGS > VOLUME. For the PC's player, the volume of
  that program in the Windows mixer is changed.
- **SETTINGS** (main menu):
  - Music source: card MP3 or PC player.
  - The PC's address, e.g. `192.168.1.20`. PicoAmp for Windows shows it under **K**; the port
    `:8765` is added. Typing it as PicoAmp for Windows shows it (`http://192.168.1.20:8765/`)
    works too.
  - TEST THE PC checks the connection.
  - Your USDB name and password.
  - Volume and mic delay (the mic delay moved here from the main menu).
- Needs PicoAmp for Windows with the PicoSing additions (`/sing/open` and `/usdb/`).

**Version 0.6: practice and tidy-up.**
- **Freeze fixed:** the game froze when a song's preview (or a jump) landed
  in the middle of some MP3s, such as Ace of Base's *All That She Wants*.
  - Two audio bytes that happen to look like the start of an MP3 frame
    fooled the MP3 decoder into copying a negative length, which crashed
    the Pico.
  - Only real frames now reach the decoder: a valid header, with the next
    frame where its length says. Tested by jumping to every second of that
    song.
- **While singing:**
  - SPACE pauses and resumes.
  - **N** (or right) jumps to 2 s before the next line, to skip an intro or
    a solo.
  - **P** (or left) goes back to the start of the line to sing it again, or
    to the line before when the current one has only just begun. The
    points for the part sung again are taken back, so it counts once.
- **MP3 tags:** **E** on the song's details screen edits title, artist,
  album and year in the MP3.
  - The cover picture and all other tag content stay.
  - When the tag has no room, the MP3 is rewritten (a few seconds).
  - An ID3v1 tag at the end gets the new text too.
- **Delete a song:** DEL in the song list, on the details screen or on the
  results screen, then **Y**.
  - When the folder holds only this song, the whole folder goes (music,
    cover and all).
  - When it holds more songs (a duet version, say), only that `.txt` goes.
- **Song folder** (main menu): choose which folder on the card the songs
  come from, e.g. `/KARAOKE/80s` or `/KARAOKE/To review`. Songs are found
  up to 4 folders deep, and the choice is remembered. Folders on a PC (via
  PicoAmp for Windows) are planned next.
- **Credits** on the title screen, their letters changing colour on the
  beat.
- When a song has no cover, its `#BACKGROUND` picture is used (if it's a
  baseline JPEG).
- Fix: PicoSing's own `PICOSING` data folder is no longer scanned for songs.

**Version 0.5: the party version.**
- **Title screen** with a flowing full-screen plasma, a rainbow logo that
  flashes on the beat and a scroller, plus an original title tune played by
  the Pico. The movement comes from palette changes (32 bytes a frame), so
  it runs smoothly even on a V9938.
- **Main menu:** SING, SINGERS, SCORES, MIC DELAY.
- **Singers:** up to 8 names, each with one of 12 pixel faces. They are
  saved on the card (`/KARAOKE/PICOSING/SINGERS.TXT`).
- **Who sings:** before each song, pick a singer for each mic (or nobody).
- **Song details:** shows the cover, title, artist, album, year, genre,
  length, language, and the 3 best scores for that song. A preview of the
  music plays from `#PREVIEWSTART`. Left/right flicks through the songs
  with a sliding cover.
  - The cover comes from the `.txt`'s `#COVER`, then
    `Artist - Title [CO].jpg`, `cover.jpg` or `folder.jpg`, then the
    picture inside the MP3 (ID3).
  - The cover is reduced to 96x96 pixels with 8 colours of its own, and
    dithered.
- **While singing:** a comment pops up after each line (PERFECT!, GREAT!,
  GOOD, KEEP GOING...), the lanes pulse on the beat, and singers' names
  show next to the scores.
- **Scores:** every result is saved to `/KARAOKE/PICOSING/SCORES.TXT`,
  the history used for each song's best scores. The SCORES screen shows
  tonight's totals per singer.
- **Fix:** in rare cases, part of a new screen stayed wrong until the slow
  row refresh reached it. The colourful title screen made this show up
  in the emulator test.

**Version 0.4:** the moving white line no longer breaks up.
- The MSX now draws each screen update right after the frame interrupt,
  while the VDP isn't drawing the picture. It asks for the next update
  straight away, so the Pico has a whole frame to prepare it.
- Rows are copied to VRAM faster (about 115 instead of 177 CPU cycles per
  row).
- In the emulator test, with realistic Pico and BIOS timing, torn updates
  went from 27% to 2% at 50 Hz, and from 72% to 1% at 60 Hz. What remains
  are the big updates when a new lyric line comes in.

**Version 0.3.** Version 0.2 ran on the real hardware (an NMS 8245 with
UltraStar songs from USDB). 0.3 fixes what showed up there:
- **Leftover pixels:** traces of earlier screens no longer stay on screen.
  - The cause: when the Pico was busy for over 0.3 s (loading a song), the
    MSX asked again and could skip a display list.
  - Now the MSX says which list it has run, and the Pico waits for that
    before writing the next one.
  - Two rows of the screen are also sent again with every list, and each
    new screen in full, so anything left behind repairs itself.
- **Held syllables:** a `~` at the start ("drea" + "~ms") is no longer
  shown, and the space after a lone `~` still ends the word.
- **Long lyric lines and titles:** up to 36 characters fit on one row.
- **Mic delay:** each song measures it. The results screen shows the best
  delay for your setup; press `D` to use it.
- **A silent mic** counts as "did not sing" (room noise no longer gives it
  a score).

## What you need

- An MSX2 (SCREEN 5), the PicoVerse 2350, and a microSD card in it
- The SingStar USB dongle with the wired mics, on the cartridge's USB-C
  port (USB-C OTG adapter). Blue = left = mic 1, red = right = mic 2.
- Something to hear the music on: the PicoVerse's 3.5 mm jack (the music
  doesn't come through the MSX's own sound output).

## Installing

1. **Flash** `bin/picosing.uf2` like the other PicoVerse firmwares (BOOTSEL,
   then copy it to the RP2350 drive). Re-flash Explorer or PicoAmp to go back.
2. **Copy songs** to the card's **first partition**, one folder per song:
   ```
   /KARAOKE/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.txt
   /KARAOKE/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.mp3
   ```
   The included `KARAOKE` folder has a test song, plus a duet version of it,
   to try first.
3. **Boot** the MSX with the cartridge in. The screen stays black for a
   moment while the Pico reads the card, then the title screen appears.

## Playing

**Title** → SPACE (or fire) → **main menu**:
- **SING:** opens the song list, then the song's details, then who sings.
- **SINGERS:** set names and faces.
- **SCORES:** tonight's totals.
- **SONG FOLDER:** where the songs come from.
- **FIND SONGS (USDB):** search and save songs from USDB.
- **SETTINGS:** music source, the PC, the USDB login, volume, mic delay.

| Key | Song list | Details | Who sings | Singing | Results |
|---|---|---|---|---|---|
| Up / Down | choose | previous / next song | blue / red mic | | |
| Left / Right | page up / down | previous / next song | choose the singer | line back / next line | |
| A-Z | jump to an artist | | | | |
| RETURN, joystick A | details | choose singers | start | | back to the list |
| SPACE | details | choose singers | start | pause / go on | back to the list |
| `N` / `P` | | | | next line / line again | |
| `E` | | edit the MP3's tags | | | |
| DEL | delete the song | delete the song | | | delete the song |
| ESC, joystick B | main menu | back to the list | back | stop | back to the list |
| `+` / `-` | | | | mic delay ±10 ms | |
| Up / Down (singing) | | | | volume | |
| `,` / `.` | | | | the PC's sound earlier / later | |
| `D` | | | | | use the measured mic delay |

**Tag editor:** up/down picks a field, type to add, BS deletes a letter,
DEL clears the field, SAVE writes the tags, ESC leaves without saving.

**Song folder:** RETURN opens a folder, BS goes up, and "SING FROM THIS
FOLDER" makes the songs come from there.

**Singers screen:** type to edit the selected name (BS deletes), left/right
changes the face, DEL removes a singer, "+ ADD A SINGER" adds one, and ESC
saves.

- **Screen:** the notes of the current line are bars, with the white line
  marking where the music is. Your pitch is drawn over them: blue mic on the
  upper half, red mic on the lower half, brighter where it hits the note.
- **Lyrics:** sung syllables turn yellow and the current one cyan. The
  next line is below in grey.
- **Duets** (`P1`/`P2` in the .txt) get two lanes: blue sings the top
  one, red the bottom one.
- **Scoring** follows UltraStar: up to 10000 points. 9000 come from the
  notes (golden notes count double), 1000 from complete lines.
  - A note counts when you're within a semitone of it, in any octave, so
    men and women can sing the same song.
  - Rap notes only need your voice; freestyle notes give no points.
- **Mic delay** (default 60 ms) shifts your voice against the notes.
  After each song the results screen shows the delay that matched your
  singing best: the game tries every delay from 0 to 400 ms against the
  notes. Press `D` to use it.

## Getting songs

PicoSing reads **UltraStar** songs, the format used by UltraStar Deluxe,
Vocaluxe and UltraStar Play: a `.txt` with the notes and lyrics, plus the
song's audio. This makes your own library possible:

- **USDB** (usdb.animux.de, free account) has the largest collection of
  `.txt` files. They come without music. The *USDB Syncer* app downloads
  the `.txt` and fetches the audio for each song. You supply the music
  yourself, from your own collection.
- **UltraSinger** makes a `.txt` from an audio file automatically (AI).
  It's handy for songs nobody has written up yet.
- **Audio** must be **MP3** (or WAV). Convert other formats with ffmpeg,
  and put the new name in the `.txt`'s `#MP3:`/`#AUDIO:` line:
  ```
  ffmpeg -i "song.m4a" -b:a 192k "song.mp3"
  ```
- Up to 400 songs. Duets show `DU` in the list. Accented letters work
  (UTF-8 and older Windows-1252 files).

Song lyrics are copyrighted, so keep downloaded songs in your own library
rather than publishing them.

**Covers:** USDB Syncer also downloads covers (`[CO].jpg`). Covers must be
baseline JPEGs, not progressive ones; resave a progressive one in any image
editor. PNG covers aren't supported.

**YouTube:** UltraStar files sometimes include a `#VIDEO` with a YouTube
link. PicoSing doesn't download from YouTube: YouTube's terms don't allow
it, and the Pico can't decode or convert those streams anyway. Use your
own MP3s. Videos can't be shown either, because the MSX2 can't display
video.

## How it works

```
MSX2 (picosing.asm, 0.5 KB of code)        PicoVerse 2350 (RP2350)
  SCREEN 5, keys, joystick   --7F80h-->    core 0: MSX bus (PIO), ROM,
  "next list"                --7F81h-->            list window 8000h-BFFFh
  runs the display list      <--8000h--    core 1: USB mics -> pitch (YIN)
  (HMMV fills, OTIR copies,                        microSD -> song list, .txt
   palette)                                        MP3/WAV -> I2S (PicoAmp's decoder)
                                                   game: timing, scoring, drawing
                                                   picture -> display list
```

- **The picture:** the Pico draws the whole screen in its own RAM, a
  256×212 picture in 16 colours. It also keeps a copy of what the MSX
  currently shows. Each frame the MSX asks for a list. The Pico sends the
  differences, at most 6 KB per list, as VDP operations: fills (HMMV),
  copies (OTIR) and the palette. The MSX code is generic and stays small;
  everything else runs and is tested on the PC.
- **The song clock:** it starts when the first decoded music reaches the
  I2S output. It never runs ahead of what was decoded, so the notes stay
  with the music even if the card is slow.
- **Code from elsewhere:**
  - Cristiano Goncalves: the MSX bus PIO programs, pin setup and SD card
    pins.
  - PicoAmp (Jzzy): the MP3/WAV decoder and I2S output, unchanged.
  - The SingStar test: the USB mic driver (with its TinyUSB patch) and the
    pitch detection.

## Building

Needs Pico SDK 2.3.x with `patches/tinyusb-rp2-host-iso.patch` applied to
`lib/tinyusb`, plus pico-extras, sjasmplus and Python 3. The library
folders are the same as in the PicoVerse firmware.

```sh
cd msx && sjasmplus picosing.asm && cd ..            # msx/PICOSING.ROM
python3 tools/bin2h.py msx/PICOSING.ROM pico/msx_rom.h
mkdir -p pico/lib && cd pico/lib
git clone https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
git clone https://github.com/ikjordan/picomp3lib
cd ../..
cmake -S pico -B build -G Ninja && ninja -C build    # build/picosing.uf2
```

The included UF2 was built with clang 18 and picolibc.
- Clang only needs one adjustment: the SD library's SDIO driver is left
  out (`sd_stubs.c` stands in for it). The PicoVerse uses SPI for its card,
  so nothing is lost.
- `sd_stubs.c` also provides `sbrk()` for picolibc.
- A normal Arm GCC build uses the same files.

## Tests (on a PC)

```sh
G="pico/game/game.c pico/game/song.c pico/game/fb.c pico/game/dlist.c pico/game/library.c
   pico/game/screen_link.c pico/game/menus.c pico/game/title.c pico/game/singers.c
   pico/game/cover.c pico/game/id3.c pico/game/tjpgd/tjpgd.c pico/game/settings.c
   pico/game/scratch.c pico/game/music.c pico/game/pclink.c pico/game/usdb.c
   pico/net/esp.c pico/net/web.c tests/fake_esp.c"
I="-Ipico -Ipico/game -Ipico/game/tjpgd"
gcc -O2 -o test_song tests/test_song.c pico/game/song.c -lm && ./test_song
gcc -O2 $I -D_GNU_SOURCE -o test_id3 tests/test_id3.c tests/simplat.c $G -lm && ./test_id3 <some.mp3> /tmp
gcc -O2 $I -o sim tests/sim.c tests/simplat.c $G -lm && ./sim songs out
gcc -O2 $I -o test_online tests/test_online.c tests/simplat.c $G -lm && ./test_online songs out
gcc -O2 -o test_net tests/test_net.c tests/fake_esp.c pico/net/esp.c pico/net/web.c && ./test_net
gcc -O2 $I -I<z80> -o e2e tests/e2e.c tests/simplat.c <z80>/z80.c $G -lm && ./e2e msx/PICOSING.ROM songs out
```

- **test_song:** UltraStar parsing. It covers the tags, the note types,
  `~`, line breaks, duets, `#RELATIVE`, UTF-8 with a byte-order mark,
  Windows-1252, decimal commas and timing.
- **test_id3:** changes the tags of v2.3 and v2.4 files, with and without
  room in the tag and with no tag at all. It checks that the picture, the
  other frames and the audio stay byte for byte, and that ID3v1 is
  updated.
- **test_net:** the HTTP client over a simulated ESP-01. It covers GET, a POST that redirects,
  cookies, 50 KB, a 404, a stream that goes on beside another request, and an unknown host.
- **test_online:** USDB and the PC's player, against a fake USDB and a fake PicoAmp for Windows.
  - The settings screen, a wrong password, then searching.
  - Saving a song: the session runs out on the way and PicoSing logs in again; the `.txt` is
    checked byte for byte, and the cover is saved too.
  - Singing it with the "PC's" video. PicoSing keeps within 60 ms of the player's time, and the
    singers score over 9000.
  - Volume, pause and play go to the player, and it pauses at the end.
  - USDB through the PC when the ESP can't reach it, HTML entities, two pages of results, and a
    Spotify search for a song without a video.
- **sim:** the whole flow, from the title through the menus, singer
  names, the details screen with its cover, the song and the duet, to the
  scores screen. Then pause, skip and sing a line again (the points
  count once), choose a folder, delete songs (a whole folder, or only a
  duet's `.txt`) and edit a tag, all on a copy of the songs. It uses simulated singers and checks
  the scores (an in-tune singer an octave low gets 9772; one going a tone
  flat half the time gets 4810). It also checks that the screen rebuilt
  from the display lists equals the Pico's picture, and saves screenshots.
- **e2e:** the real MSX ROM on a Z80 emulator with a V9938 model, its
  cartridge window served by the game code.
  - The VRAM ends up identical to the Pico's picture.
  - With a Pico that stalls for 300-500 ms now and then, the old handshake
    loses lists (83 rows wrong); the new one loses none.
  - A TV-beam model counts torn updates: lists written partly before and
    partly after the picture passed those rows. It uses 50 Hz and 60 Hz
    timing, the real Pico's 2-8 ms delay and the BIOS interrupt's cost.
  - Screens keep up at about 58 lists a second, 9 KB/s while singing.
  - The menu appears 0.5 s after the MSX starts the ROM.

## Licence

CC BY-NC-SA 4.0, as the PicoVerse and PicoAmp. font8x8 (Daniel Hepper)
is public domain. The TinyUSB patch is under TinyUSB's MIT licence. The test
song is a traditional melody with words by Jane Taylor (1806); the music
was synthesized for this project.
