# Architecture — sappkeys

<!-- UPDATE WHEN: stack, components, or data flow change -->

## Stack

- C++20, CMake ≥ 3.24.
- **SappSounds** (sibling checkout `../sappsounds`, or FetchContent fallback)
  — SFZ parse, sample decode, realtime voice engine, sustain-pedal semantics,
  offline render plumbing, WAV/MIDI IO.
- **JUCE 8.0.15** (FetchContent, pinned — same tag as sappsynth/sapporchestra;
  local builds reuse the sappsynth checkout via
  `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/apps/sappaudio/sappsynth/build/_deps/juce-src`).
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
- `src/plugin/UpdateManager.h` — in-plugin updater (background
  thread): GitHub latest-release check vs JucePlugin_VersionString,
  platform-asset download, install (SappKeys.vst3/.component on
  macOS + xattr -rc; Windows rename-trick swap), standalone
  self-relaunch on macOS. `src/core/VersionCompare.h` does the
  semver-ish tag comparison.

## Key policies (where behavior lives)

- **Velocity/touch** — `shapeVelocity()` in KeysEngine.h; shared by the DSP
  and the editor's velocity-curve display.
- **Mechanical noises** — release-trigger regions get `gain_cc102 = -60 dB`
  injected at load (`applyKeysPolicy`); the engine drives CC 102 from the
  `mechNoise` param and **drops external CC 102**.
- **Pedals** — CC64 is engine-native (SappSounds holds notes + defers release
  samples; KeysEngine wakes the resonance bank). Una corda is the `unaCorda`
  parameter, reachable via SappLink CC 67 (MMA soft pedal).
- **Imperfection master (`clean`)** — `applyClean()` in KeysEngine.h scales
  every modeled imperfection by (1 − clean): `mechNoise` and `vintage`, the
  complete list. `process()` runs the whole KeysParams block through it before
  any DSP reads a value, so nothing downstream can miss it. SappLink CC 3,
  suite-wide (sapptune #30).
- **Reserved CCs** — 1 (dynamics), 11 (expression), 64 (sustain) engine-native;
  102 internal; 3 is the suite-wide `clean` lane. Everything else automatable
  per the SappLink manifest.

## Data flow

MIDI → (plugin: APVTS + CC slews | CLI: SappLink applyCcToParams) →
KeysEngine.process: applyClean (imperfection scaling) → velocity shaping → PlaybackEngine (samples) → tone filter
(dynamics/una-corda/vintage) → lid shelf → width → wow/flutter gain →
drive → sympathetic resonance (added) → early + small-room FDN → master →
safety limiter → output guard.

## Output safety

`KeysEngine::limitAndGuard`. The Safety Limiter parameter (default ON) is
peak-accurate gain reduction to -1 dBFS: the block is rendered first and the
gain comes from the block's own peak, so no sample gets past the ceiling and
no latency is added. It replaced a `tanh` soft-clipper that held the peak at
0 dBFS but turned a dense burst into a full-scale square wave. After it,
unconditionally: non-finite samples become 0 (and every filter/delay state is
scrubbed), and the output is clamped to ±1 whether the limiter is on or off.

Two MIDI-burst guards sit in `KeysEngine::process`. `kMaxBlockEvents` — a block
carrying more events than this is a flood, so the surplus is dropped and an
AllSoundOff ends every voice, because the events that get truncated are the
note-offs. `kNoteOffGuardSeconds` — a note-off is held back until its note-on
is 8 ms old, because SappSounds starts a *stolen* voice only when its 3 ms
steal fade finishes, and a note-off arriving inside that window finds no active
voice and is lost, leaving the note sounding forever.
