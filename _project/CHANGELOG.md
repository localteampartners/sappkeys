# Changelog — sappkeys

<!-- UPDATE WHEN: anything meaningful ships -->

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
