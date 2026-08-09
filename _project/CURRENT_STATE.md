# Current state — sappkeys

<!-- UPDATE WHEN: something ships, breaks, or gets fixed -->

## Shipped 2026-08-09 — v0.8.0: load-window gating + postmortem guards

- GitHub issues #1 and #2 closed. `StartupGate` now gates note-ons across
  EVERY async instrument load (program change, preset parameter, user
  preset, SFZ pick), not just state restore: notes in a load window are
  suppressed until the new instrument installs; sounding notes fade via the
  engine's swap steal-fade. A failed mid-session load re-arms onto the
  still-installed real instrument; a failed restore over the diagnostic
  stays silent.
- New host parameter `libraryReady` (readable, non-automatable, appended
  last, NOT in APVTS/state) — sappradio can poll readiness instead of a
  blind settle window.
- Host logs carry build identity: `SappKeys-build: version=…` at
  construction; `build=…` on `SappKeys-audio-source` lines.
- verify.sh now fails when no build dir has `SAPPKEYS_BUILD_PLUGIN=ON` and
  loudly warns when the installed VST3 is older than the newest src/
  commit (the postmortem's local-install hole).
- Tests 49/49; auval (18 parameters) passes; installed VST3 refreshed.

## Shipped 2026-08-09 — instrument-state safety, sapptune#21

- Root cause of the "default sound" idle blast: the construction-default
  Diagnostic Orchestra is playable between plugin instantiation and the
  async SFZ load kicked off by `setStateInformation`. `StartupGate`
  (`src/core/StartupGate.h`) now suppresses note-ons until the restored
  instrument is installed (fresh inserts arm after a 1.5 s grace); MIDI
  program changes are deferred across the same window.
- Instrument identity is logged when a voice batch starts from silence
  (`SappKeys-audio-source: instrument="..." gen=N armed=X voices=V`) and
  when the gate suppresses note-ons (`SappKeys-midi-gate: suppressed=N`).
  On the Windows machine, grep Live's Log.txt for `SappKeys-audio-source`
  on the next recurrence.
- `replaceState` preset-listener echo guarded (restore no longer re-applies
  the preset over restored state); `collectRetired()` deferred until the
  audio thread rendered past the swap. Tests 43/43, auval, uishot both
  modes pass. Needs a Windows build ≥ v0.6.2 to reach the reporter.

## Shipped 2026-08-08 — user presets (uncommitted)

- Suite-shared user-preset format (sapptune/sapplink/PRESETS.md) in
  `src/plugin/UserPresets.{h,cpp}` (verbatim copy of sappsynth's), wired
  through `SappKeysProcessor`: `saveUserPreset` / `loadUserPreset` /
  `userPresets()` / `applyPresetChoice`, plus a `preset`
  AudioParameterChoice added LAST in `makeLayout()`. No existing
  parameter id, order or CC moved.
- Presets store normalised values plus the loaded SFZ path (`sfz`), so a
  saved sound restores its sample library when it is still installed.
- Editor footer has a PRESET chooser (fresh disk scan on open; user
  entries read "<name> (user)") and a SAVE button with an async name
  dialog.
- Proven headless by `SappKeysUiShot --presettest`: 16/16 parameters
  round-trip with max |diff| = 0; `preset` parameter, MIDI program
  change and host-state round-trip all still good. AU passes
  `auval -v aumu Skys Ltpr`.

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
