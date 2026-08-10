# Spec — sappkeys

<!-- UPDATE WHEN: scope, goals, or non-goals change -->

## What

A beautiful piano & electric-keys instrument (JUCE Standalone / VST3 / AU)
built on the SappSounds sample engine, following the sapporchestra
architecture: a framework-free core policy layer, a thin JUCE wrapper, a
deterministic agent CLI, and a SappLink manifest so sapptune can drive it.

## Goals

- **Playable and beautiful.** Salamander Grand (CC-BY) as the flagship
  library; free FreePats keyboards (FM EP, upright, old piano — all CC0)
  fetched by script, never committed.
- **Piano-specific policy in the core** (no JUCE): touch velocity curve,
  una-corda softening, lid tilt + width, sympathetic resonance on pedal-down,
  mechanical-noise mix (release samples), small-room ambience (not a hall),
  tape/vintage EP character + gentle drive, and a `clean` master that scales
  every modeled imperfection by (1 − clean).
- **Agent-first API.** `sappkeys` CLI: inspect / validate / params / presets /
  scan / render — one JSON document per command, seeded deterministic renders.
- **SappLink contract.** Manifest at
  `~/apps/sapptune/sapplink/manifests/sappkeys.json`; CC1/CC11/CC64 stay
  engine-native; CC 3 is the suite-wide `clean` lane; drift guarded by a unit
  test against a vendored copy.
- **Quiet by default.** Modeled imperfection is character, not a level: it
  ships low enough that several sapp* instruments in one unattended mix don't
  stack into audible grain, and `clean` removes it entirely on request.

## Non-goals

- No keyswitch/articulation UI (piano libraries don't use them; the engine
  still reports them via `inspect`).
- No concert-hall reverb — the room is deliberately small; use sapporchestra
  for stage/hall placement.
- No sample streaming or custom sample editing; SappSounds owns all
  sample/SFZ behavior.
- No VPS/service surface: this is a desktop instrument + CLI.

## Users

- Michael, playing and producing.
- MIDI-generation agents (sapptune) rendering piano/EP parts offline.
