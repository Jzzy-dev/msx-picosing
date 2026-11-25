#!/usr/bin/env python3
"""Make the PicoSing test song: "Twinkle, Twinkle, Little Star" (traditional
melody, words by Jane Taylor, 1806 - public domain) as an UltraStar .txt with
synthesized music (piano-like melody, bass and chords) as MP3.

usage: make_test_song.py <folder>
"""
import sys, os, subprocess, wave
import numpy as np

BPM = 100                    # real tempo, quarter notes
US_BPM = BPM                 # UltraStar #BPM (a beat = a quarter of a quarter note)
GAP_MS = 2400                # 4 count-in clicks
RATE = 44100
BEAT_MS = 60000 / (US_BPM * 4)   # 150 ms

# melody: (pitch, quarters, syllable); UltraStar pitch 0 = C4
C, D, E, F, G, A = 0, 2, 4, 5, 7, 9
P1 = [(C,1,"Twin"),(C,1,"kle, "),(G,1,"twin"),(G,1,"kle, "),(A,1,"lit"),(A,1,"tle "),(G,2,"star,")]
P2 = [(F,1,"How "),(F,1,"I "),(E,1,"won"),(E,1,"der "),(D,1,"what "),(D,1,"you "),(C,2,"are!")]
P3 = [(G,1,"Up "),(G,1,"a"),(F,1,"bove "),(F,1,"the "),(E,1,"world "),(E,1,"so "),(D,2,"high,")]
P4 = [(G,1,"Like "),(G,1,"a "),(F,1,"dia"),(F,1,"mond "),(E,1,"in "),(E,1,"the "),(D,2,"sky.")]
PHRASES = [P1, P2, P3, P4, P1, P2]
CHORDS = [  # one chord per half bar (2 quarters), per phrase
    ["C","C","F","C"], ["F","C","G","C"], ["C","F","C","G"],
    ["C","F","C","G"], ["C","C","F","C"], ["F","C","G","C"]]
CHORD_NOTES = {"C": [0, 4, 7], "F": [5, 9, 12], "G": [7, 11, 14]}

def txt():
    out = ["#TITLE:Twinkle Twinkle Little Star", "#ARTIST:Traditional",
           "#MP3:Twinkle Twinkle Little Star.mp3", "#AUDIO:Twinkle Twinkle Little Star.mp3",
           "#COVER:cover.jpg", "#PREVIEWSTART:4",
           "#LANGUAGE:English", "#GENRE:Children", "#YEAR:1806",
           "#BPM:%d" % US_BPM, "#GAP:%d" % GAP_MS]
    beat = 0
    for i, ph in enumerate(PHRASES):
        for k, (p, q, syl) in enumerate(ph):
            kind = "*" if (i == len(PHRASES) - 1 and k == len(ph) - 1) else ":"
            length = q * 4 - 1
            out.append("%s %d %d %d %s" % (kind, beat, length, p, syl))
            beat += q * 4
        if i < len(PHRASES) - 1:
            out.append("- %d" % beat)
    out.append("E")
    return "\n".join(out) + "\n"

def duet_txt():
    """the same song as a duet: blue sings lines 1, 3, 5, red 2, 4, 6"""
    head = ["#TITLE:Twinkle Twinkle Little Star (Duet)", "#ARTIST:Traditional",
            "#MP3:Twinkle Twinkle Little Star.mp3", "#AUDIO:Twinkle Twinkle Little Star.mp3",
            "#P1:Blue", "#P2:Red", "#LANGUAGE:English", "#GENRE:Children", "#YEAR:1806",
            "#BPM:%d" % US_BPM, "#GAP:%d" % GAP_MS]
    starts, beat = [], 0
    for ph in PHRASES:
        starts.append(beat)
        beat += sum(q for _, q, _ in ph) * 4
    out = []
    for player in (0, 1):
        out.append("P%d" % (player + 1))
        mine = [i for i in range(len(PHRASES)) if i % 2 == player]
        for k, i in enumerate(mine):
            b = starts[i]
            for p, q, syl in PHRASES[i]:
                out.append(": %d %d %d %s" % (b, q * 4 - 1, p, syl))
                b += q * 4
            if k < len(mine) - 1:
                out.append("- %d" % b)
    return "\n".join(head + out + ["E"]) + "\n"

def tone(freq, dur, amp, harmonics=(1, .5, .25, .12), decay=3.0):
    t = np.arange(int(dur * RATE)) / RATE
    s = sum(a * np.sin(2 * np.pi * freq * (h + 1) * t) for h, a in enumerate(harmonics))
    env = np.exp(-decay * t) * np.minimum(1, t / 0.005)
    rel = np.minimum(1, (dur - t) / 0.03)
    return amp * s * env * np.clip(rel, 0, 1)

def midi_hz(m): return 440.0 * 2 ** ((m - 69) / 12)

def audio(path):
    q = 60.0 / BPM
    total = GAP_MS / 1000 + sum(sum(n[1] for n in ph) for ph in PHRASES) * q + 2.5
    L = np.zeros(int(total * RATE) + RATE)
    def add(sig, at):
        i = int(at * RATE); L[i:i + len(sig)] += sig[:len(L) - i]
    for k in range(4):                              # count-in clicks
        add(tone(1760 if k == 0 else 1320, 0.05, 0.25, (1,), 30), k * q)
    t = GAP_MS / 1000
    for ph, chords in zip(PHRASES, CHORDS):
        tt = t
        for p, quarters, _ in ph:                   # melody, like a piano
            add(tone(midi_hz(60 + p), quarters * q * 0.95, 0.30, decay=2.2), tt)
            tt += quarters * q
        for k, ch in enumerate(chords):             # chords and bass, half bars
            at = t + k * 2 * q
            add(tone(midi_hz(36 + CHORD_NOTES[ch][0]), 2 * q * 0.95, 0.22, (1, .3), 1.5), at)
            for n in CHORD_NOTES[ch]:
                add(tone(midi_hz(48 + n), 2 * q * 0.9, 0.06, (1, .2), 1.0), at)
        t = tt
    L /= max(1.0, np.max(np.abs(L)) / 0.8)
    pcm = (L * 32767).astype(np.int16)
    stereo = np.repeat(pcm[:, None], 2, axis=1)
    wav = path[:-4] + ".wav"
    with wave.open(wav, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(stereo.tobytes())
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", wav, "-codec:a", "libmp3lame",
                    "-b:a", "128k", path], check=True)
    os.remove(wav)
    return total

folder = sys.argv[1]
name = "Twinkle Twinkle Little Star"
open(os.path.join(folder, name + ".txt"), "w", newline="\n").write(txt())
open(os.path.join(folder, name + " (Duet).txt"), "w", newline="\n").write(duet_txt())
secs = audio(os.path.join(folder, name + ".mp3"))
print("wrote %s (.txt, .mp3, %.1f s)" % (name, secs))
