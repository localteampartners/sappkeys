# Changelog — sappkeys

<!-- UPDATE WHEN: anything meaningful ships -->

## 2026-08-09 — v0.5.1

- Fixed the plugin version, which was still 0.3.0 while releases had moved
  on to v0.5.0. The in-plugin updater compares the running build's version
  against the latest tag, so a v0.5.0 install reported itself as 0.3.0 and
  kept re-offering the same update after installing it. The binary now
  reports its real version; RUNBOOK already requires bumping
  `project(SappKeys VERSION ...)` with every release tag.

## 2026-08-08 — user presets

- SappLink user presets (sapptune/sapplink/PRESETS.md): save the current
  sound to `<Documents>/SappSounds/presets/sappkeys/<name>.json` as
  normalised values, load it by name from any instance.
  `src/plugin/UserPresets.{h,cpp}` is the suite-shared implementation,
  copied verbatim from sappsynth.
- New `preset` APVTS parameter (AudioParameterChoice, added LAST in the
  layout so no existing parameter index moves, no CC): factory bank in
  program order, then the user presets found at construction. Host- and
  SappLink-automatable; applied on the message thread by the existing
  30 Hz timer.
- Saved presets record the loaded SFZ library in the file's `sfz` field
  and restore it on load when the path still resolves.
- Editor footer: PRESET chooser (rescans on open, user entries marked
  "(user)") + SAVE with an async name dialog; outcome shown in the status
  line.
- Manifest gained a top-level `hostParameters` entry for `preset` (not
  `parameters` — it carries no CC).
- `SappKeysUiShot --presettest`: headless round-trip proof (capture ->
  disk -> fresh processor, max |diff| 0) plus `preset`-parameter, MIDI
  program-change and host-state regressions. `--cctest` now waits for the
  instrument load instead of a fixed 2.5 s (it measured silence on a busy
  machine).

## 2026-08-07 — v0.3.0
- In-plugin UPDATE button: daily GitHub release check (click the version
  number to check on demand); one click downloads and installs the new
  build (macOS: plug-in folders + quarantine cleared; Windows: loaded
  .vst3 swapped via rename), standalone relaunches itself on macOS.
- Plugin version now tracks release tags (0.3.0).

## 2026-08-06 (later)

- GET SOUNDS panel: in-plugin library downloads + instrument browser (ported
  from sapporchestra). Registry: Salamander Grand Piano (707 MB, CC-BY,
  tar.gz), FreePats Upright Piano KW / FM Piano 1 / Old Piano FB (CC0, zips).
  Shared Sapp samples root (~/Samples, persisted in Application Support/Sapp).
  UiShot gained `--sounds`.

## 2026-08-06

- v0.1.0: initial release.
- Core keys engine (touch curve, una corda, lid, sympathetic resonance,
  mech-noise policy, vintage/drive, small room) on SappSounds.
- `sappkeys` agent CLI (inspect/validate/params/presets/scan/render, JSON,
  seeded deterministic).
- JUCE 8.0.15 plugin (Standalone/VST3/AU) with ivory/ebony editor: velocity-
  curve display, pedal lamps, 88-key keyboard. UiShot + --cctest.
- SappLink manifest (12 params; CC1/11/64 native) + vendored drift guard.
- 28 Catch2 tests. Demo: Gymnopédie No. 1 through Salamander + FM EP.
- Added fm-piano1 / upright-piano / old-piano-fb (all CC0) to sappsounds
  fetch-library.sh.
