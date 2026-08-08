# Current state — sappkeys

<!-- UPDATE WHEN: something ships, breaks, or gets fixed -->

## Shipped 2026-08-07 — v0.3.0 (in-plugin updater)

- Footer version button checks GitHub daily (or on click); UPDATE
  button downloads + installs the newest release (macOS: plug-in
  folders + quarantine clear; Windows: rename-trick swap of the
  loaded .vst3). Throttle key `lastUpdateCheck-sappkeys` in the shared
  Sapp settings file.
- v0.3.0 GitHub release carries CI-built Windows-x64 and
  macOS-universal zips (SappKeys VST3/AU/Standalone). End-to-end
  verified 2026-08-07 (sappkeys harness: v0.2.0 -> found v0.3.0 ->
  downloaded + installed CI universal bundles, quarantine cleared).
- CMake `project()` VERSION must be bumped with every release tag
  (the updater compares JucePlugin_VersionString to the tag).
- Build dirs (`build/`, `build-plugin/`) no longer tracked in git.

## Works (2026-08-06)

- Core engine, CLI, plugin (Standalone/VST3/AU), UiShot all build clean
  (strict warnings) on macOS against sibling SappSounds + pinned JUCE 8.0.15.
- 28 Catch2 tests green (`./verify.sh` ~30 s cold).
- Salamander Grand V3 loads: 641 regions, 0 missing samples, release samples
  driven by the mech-noise knob. FM-Piano1 + Upright-Piano-KW fetched and
  render fine.
- SappLink CC-in proven end-to-end: unit tests (render path) and
  `SappKeysUiShot --cctest` (plugin path, CC7 sweep).
- GET SOUNDS panel in the plugin (port of sapporchestra's SoundsPanel):
  one-click download → extract → rescan for Salamander Grand (tar.gz) and
  FreePats Upright/FM/Old pianos (zips), plus an installed-instruments
  browser over the shared ~/Samples root (filter + double-click to load).
  `SappKeysUiShot --sounds` snapshots the panel open.
- Demo renders: `demo/gymnopedie-salamander.wav` (concert-grand preset),
  `demo/gymnopedie-ep.wav` (ep-tine preset) from `demo/gymnopedie.mid`.

## Known issues / rough edges

- Salamander's pedal-noise regions use `on_locc64/on_hicc64` — unsupported by
  SappSounds, so pedal down/up thumps are silent (key-release noises work).
  `rt_decay` and `pitch_keytrack` are also ignored (minor).
- Resonance comb release fade is tuned for 48 kHz (fixed coefficient); at
  96 kHz the pedal-up fade is ~2× longer. Cosmetic.
- UiShot needs a windowless-capable session (it is offscreen but still a GUI
  app); run from a normal user session.
- AU/VST3 not yet smoke-tested in a third-party host (Standalone verified).

## Not built (see TODO)

- Pedal down/up noise support (needs SappSounds `on_loccN` trigger support).
- Half-pedal (CC64 continuous) behavior.
- Preset save/load in the plugin UI (presets exist in the CLI only).
