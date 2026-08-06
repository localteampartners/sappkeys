# SappKeys Agent API

The `sappkeys` CLI is the stable machine interface for external software —
MIDI-generation agents in particular. Every command prints exactly one JSON
document to stdout; diagnostics go to stderr; exit codes are `0` ok,
`1` ok-with-warnings, `2` failure.

Binary: `build/sappkeys` (CMake target `sappkeys-cli`).

## Typical agent workflow

```text
1. sappkeys inspect  → learn range, velocity layers, release samples
2. compose MIDI      → notes + CC64 pedaling (+ CC11 phrasing, SappLink CCs)
3. sappkeys render   → deterministic WAV (fixed --seed)
4. judge / iterate
```

## inspect

```bash
sappkeys inspect (--sfz <file.sfz> | --diagnostic) [--regions]
```

Returns `name`, `regions`, `missingSamples`, `estimatedRamBytes`,
`playableRange {low, high, lowName, highName}`,
`capabilities {velocityLayers, roundRobins, releaseSamples,
mechNoiseControl}`, `controllers` (the CC conventions below), and
`diagnostics`. `--regions` adds a per-region dump.

Composition rules an agent should follow:

- Keep notes inside `playableRange`.
- **Pedal like a pianist**: CC64 down after the beat-1 bass, up at the bar
  change. The pedal holds notes, defers release samples, and wakes the
  sympathetic-resonance layer.
- Velocity is the instrument's dynamics. CC11 is phrase-level trim on top;
  CC1 is a gentle level+brightness trim (neutral at 127).
- Una corda (CC 67, SappLink) for hushed passages.

## validate

```bash
sappkeys validate --sfz <file.sfz>
```

`{"ok":bool, "errors":N, "warnings":N, "missingSamples":N, "regions":N,
"unsupportedOpcodes":[...], "diagnostics":[...]}`

## params

```bash
sappkeys params
```

Full parameter schema: `{"params":[{name, id, min, max, default, doc}],
"enums":{"quality":[...]}}`. Each entry carries `id` (stable APVTS parameter
ID = SappLink manifest ID) and, when reachable from MIDI, `cc` (+`ccCurve`,
or `ccNative` for engine-handled controllers).

| name | id | range | default | MIDI CC | meaning |
|---|---|---|---|---|---|
| touch | touch | 0–1 | 0.5 | 14 | velocity curve: heavy ↔ light |
| dynamics | dynamics | 0–1 | 1.0 | 1 (native) | gentle level+brightness trim |
| expression | expression | 0–1 | 1.0 | 11 (native) | phrase volume |
| una_corda | unaCorda | 0–1 | 0 | 67 | soft pedal: softer strike + felt tilt |
| lid | lid | 0–1 | 0.85 | 15 | 0 closed … 1 full stick |
| resonance | resonance | 0–1 | 0.5 | 16 | pedal-down sympathetic resonance |
| mech_noise | mechNoise | 0–1 | 1.0 | 17 | release-sample mix (1 = as recorded) |
| width | width | 0–2 | 1.0 | 18 | stereo width |
| vintage | vintage | 0–1 | 0 | 21 | tape: random tune, wow/flutter, soft HF |
| drive | drive | 0–1 | 0 | 22 | gentle saturation (EPs) |
| room_level | roomLevel | 0–1 | 0.30 | 91 | small-room ambience level |
| room_size | roomSize | 0.6–1.4 | 1.0 | 19 | room size |
| room_decay | roomDecay | 0.2–2.5 | 0.9 | 20 (log) | room T60 seconds |
| master_gain_db | masterGain | −24–12 | 0 | 7 | output gain |
| quality | quality | enum | 1 | — | 0 draft (linear) · 1 normal (cubic) |

**SappLink CC-in:** the MIDI CC column is a live contract — CCs embedded in a
rendered `.mid` (or played into the plugin) move these parameters, with slew
smoothing, on any channel. See [sapplink.md](sapplink.md) and the manifest at
`~/apps/sapptune/sapplink/manifests/sappkeys.json`. CC 102 is reserved
(internal mechanical-noise lane) and ignored from outside.

## presets

```bash
sappkeys presets
```

Named starting points (`concert-grand`, `intimate`, `felt`, `pop-bright`,
`ep-tine`, `ep-crunch`) with their param bundles. Use `render --preset NAME`;
explicit `--param` after `--preset` overrides individual values.

## scan

```bash
sappkeys scan <library-dir> [--all]
```

Walks a folder for `.sfz` instruments (skipping `includes/` partials unless
`--all`): `{"instruments":[{path, name, category, regions, releaseSamples,
lowKey, highKey}], "count":N}`.

## render

```bash
sappkeys render (--sfz <file.sfz> | --diagnostic) \
    --midi <file.mid> --out <file.wav> \
    [--sr 48000] [--seed N] [--tail seconds] \
    [--preset NAME] [--param NAME=VALUE ...]
```

- Input: SMF format 0/1. Notes, CC 1/11/64, SappLink CCs, pitch bend.
- Output: stereo float32 WAV through the full chain (sampler → touch/tone →
  resonance → drive → room → limiter).
- **Deterministic:** identical inputs + `--seed` ⇒ bit-identical WAV. Vary
  the seed for new round-robin/vintage-detune takes.

Result: `{"ok":true, "out":..., "frames":N, "durationSeconds":s, "peak":p,
"rms":r, "midiEvents":N, "seed":N}`.

## Stability

Command names, field names, exit codes, and parameter names are contracts.
New fields may be added; existing ones are not renamed or repurposed.
