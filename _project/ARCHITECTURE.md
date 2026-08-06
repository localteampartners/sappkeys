# Architecture — sappkeys

<!-- UPDATE WHEN: stack, components, or data flow change -->

## Stack

- C++20, CMake ≥ 3.24.
- **SappSounds** (sibling checkout `../sappsounds`, or FetchContent fallback)
  — SFZ parse, sample decode, realtime voice engine, sustain-pedal semantics,
  offline render plumbing, WAV/MIDI IO.
- **JUCE 8.0.15** (FetchContent, pinned — same tag as sappsynth/sapporchestra;
  local builds reuse the sappsynth checkout via
  `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/apps/sappsynth/build/_deps/juce-src`).
- **Catch2 v3.7.1** for tests.

## Components

```
src/core/            framework-free product policy (no JUCE)
  KeysEngine.*       touch curve, CC1/CC11 trims, una corda, lid, width,
                     vintage wow/flutter, drive, mech-noise CC injection,
                     chain: sampler → tone → resonance → drive → room
  Resonance.h        sympathetic resonance: 12 tuned feedback combs,
                     claimed by held/played notes while CC64 is down
  Room.h             RoomEarly (6 short taps) + SmallRoom (6-line FDN,
                     0.2–2.5 s decay) — a room, deliberately not a hall
  KeysInstrument.*   SFZ load pipeline: parse → keys policy (tag release
                     regions with internal gain CC 102) → decode samples
  SappLinkCCMap.*    the SappLink CC-in table (single source shared by
                     plugin slews, CLI render, and the drift-guard test)
  KeysRender.*       deterministic offline render incl. SappLink CC-in
src/cli/main.cpp     `sappkeys` agent CLI (JSON out, exit codes 0/1/2)
src/plugin/          JUCE processor (APVTS) + ivory/ebony editor
tools/uishot/        offscreen editor PNG + --cctest SappLink proof
tests/unit/          Catch2: engine, resonance, room, render, sapplink
tests/data/          vendored SappLink manifest copy (drift guard)
scripts/make_demo.py Gymnopédie No. 1 demo MIDI generator
```

## Key policies (where behavior lives)

- **Velocity/touch** — `shapeVelocity()` in KeysEngine.h; shared by the DSP
  and the editor's velocity-curve display.
- **Mechanical noises** — release-trigger regions get `gain_cc102 = -60 dB`
  injected at load (`applyKeysPolicy`); the engine drives CC 102 from the
  `mechNoise` param and **drops external CC 102**.
- **Pedals** — CC64 is engine-native (SappSounds holds notes + defers release
  samples; KeysEngine wakes the resonance bank). Una corda is the `unaCorda`
  parameter, reachable via SappLink CC 67 (MMA soft pedal).
- **Reserved CCs** — 1 (dynamics), 11 (expression), 64 (sustain) engine-native;
  102 internal. Everything else automatable per the SappLink manifest.

## Data flow

MIDI → (plugin: APVTS + CC slews | CLI: SappLink applyCcToParams) →
KeysEngine.process: velocity shaping → PlaybackEngine (samples) → tone filter
(dynamics/una-corda/vintage) → lid shelf → width → wow/flutter gain →
drive → sympathetic resonance (added) → early + small-room FDN → master →
soft limiter.
