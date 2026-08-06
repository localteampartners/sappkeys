#!/usr/bin/env python3
"""Write the SappKeys demo MIDI: Erik Satie, Gymnopedie No. 1 (opening).

Public-domain music chosen to show the product policy: sustain pedal on every
bar (sympathetic resonance + deferred releases), CC11 phrase shaping, soft
chord voicings, and gently rolled chords. Deterministic output: no randomness
here, and the renderer is seeded.

Usage:
  python3 scripts/make_demo.py [out.mid]
Then:
  sappkeys render --sfz <salamander.sfz> --midi demo/gymnopedie.mid \
      --out demo/gymnopedie.wav --preset concert-grand --seed 20260806
"""

import struct
import sys

DIVISION = 480              # ticks per quarter
TEMPO_US = 900_000          # 66.7 bpm — Lent
BEATS_PER_BAR = 3

# Note helpers -----------------------------------------------------------
NAMES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5,
         "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "Bb": 10, "B": 11}


def n(name: str) -> int:
    """'F#4' -> MIDI note (C4 = 60)."""
    pitch = name.rstrip("-0123456789")
    octave = int(name[len(pitch):])
    return NAMES[pitch] + (octave + 1) * 12


events = []  # (tick, priority, bytes)


def at(bar: float, beat: float) -> int:
    return round((bar * BEATS_PER_BAR + beat) * DIVISION)


def note(bar, beat, name, dur_beats, vel, roll_ticks=0):
    on = at(bar, beat) + roll_ticks
    off = at(bar, beat + dur_beats)
    events.append((on, 1, bytes((0x90, n(name), vel))))
    events.append((off, 0, bytes((0x80, n(name), 0))))


def cc(bar, beat, num, value):
    events.append((at(bar, beat), 0, bytes((0xB0, num, value))))


# The music ---------------------------------------------------------------
# Left hand: the swaying G/D alternation. Chords sit on beats 2-3, softly.
def accompaniment(bar, which):
    if which == "G":
        bass, chord = "G2", ("B3", "D4", "F#4")
    else:
        bass, chord = "D2", ("A3", "C#4", "F#4")
    note(bar, 0.0, bass, 2.0, 58)
    for i, name in enumerate(chord):        # gentle upward roll, ~12 ms steps
        note(bar, 1.0, name, 1.9, 46 + i * 2, roll_ticks=i * 7)


# Melody phrase (bars are 0-based offsets from its entry).
PHRASE = [
    (0, 0.0, "F#5", 1.0, 76), (0, 1.0, "A5", 1.0, 72), (0, 2.0, "G5", 1.0, 70),
    (1, 0.0, "F#5", 1.0, 74), (1, 1.0, "C#5", 1.0, 66), (1, 2.0, "B4", 1.0, 64),
    (2, 0.0, "C#5", 1.0, 66), (2, 1.0, "D5", 1.0, 68), (2, 2.0, "A4", 1.0, 62),
    (3, 0.0, "F#4", 3.0, 58),
]

TOTAL_BARS = 14

# Sustain pedal: down just after each bar's bass, lifted at the bar change
# (legato pedaling — the engine re-catches the new harmony).
for bar in range(TOTAL_BARS - 1):
    cc(bar, 0.10, 64, 100)
    cc(bar + 1, -0.02, 64, 0)
cc(TOTAL_BARS - 1, 0.10, 64, 100)
cc(TOTAL_BARS - 1, 2.9, 64, 0)

# Expression: a slow breath per 4-bar phrase.
for bar in range(TOTAL_BARS):
    phase = bar % 4
    cc(bar, 0.0, 11, (108, 116, 122, 112)[phase])

# Bars 0-3: introduction, accompaniment alone.
for bar in range(TOTAL_BARS - 1):
    accompaniment(bar, "G" if bar % 2 == 0 else "D")

# Melody enters at bar 4, phrase repeats at bar 8.
for entry in (4, 8):
    for dbar, beat, name, dur, vel in PHRASE:
        note(entry + dbar, beat, name, dur, vel, roll_ticks=8)

# Bar 12: closing gesture; bar 13: final D-major color, held under pedal.
note(12, 0.0, "A4", 1.0, 64, roll_ticks=8)
note(12, 1.0, "B4", 1.0, 62, roll_ticks=8)
note(12, 2.0, "C#5", 1.0, 60, roll_ticks=8)
for i, name in enumerate(("D3", "A3", "D4", "F#4", "A4")):
    note(13, 0.0, name, 2.8, 54 - i * 2, roll_ticks=i * 9)

# SMF format 0 writer -----------------------------------------------------


def vlq(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(out))


def build_track() -> bytes:
    data = bytearray()
    data += vlq(0) + b"\xff\x51\x03" + struct.pack(">I", TEMPO_US)[1:]
    data += vlq(0) + b"\xff\x58\x04" + bytes((3, 2, 24, 8))  # 3/4
    last = 0
    for tick, _prio, msg in sorted(events, key=lambda e: (e[0], e[1])):
        tick = max(0, tick)
        data += vlq(tick - last) + msg
        last = tick
    data += vlq(DIVISION) + b"\xff\x2f\x00"
    return bytes(data)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "demo/gymnopedie.mid"
    track = build_track()
    with open(out, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, DIVISION))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + track)
    beats = TOTAL_BARS * BEATS_PER_BAR
    print(f"wrote {out}: {len(events)} events, "
          f"{beats * TEMPO_US / 1e6:.1f}s at {60e6 / TEMPO_US:.1f} bpm")


if __name__ == "__main__":
    main()
