# Handoff — sappkeys

<!-- UPDATE WHEN: starting/finishing multi-step work, or before ending a session mid-task -->

Work in flight: GitHub issue #3 — SappLink `clean` (CC 3), Mechanics default,
vintage CC 21 → CC 12. Target release v0.9.0 (commit + push, **no tag** — the
build host is down, the release fires when the self-hosted runner is back).

Suite-wide convention (sapptune#30), fixed — do not deviate:
`clean` 0..1, default 0, CC 3, scales EVERY modeled-imperfection source by
(1 − clean). clean 0 = today's behavior exactly; clean 1 = no modeled noise,
wear or jitter. Sibling agents implement the same in sappedal / sapporchestra;
sapptune already updated `sapplink/manifests/sappkeys.json` (vintage on CC 12,
clean on CC 3 — verified 2026-08-09).

Plan:

- [ ] `KeysParams::clean` + `applyClean()` helper in `src/core/KeysEngine.h`;
      `process()` runs params through it once, so nothing downstream can miss it.
      Imperfection sources: `mechNoise` (release/mechanical/pedal-noise regions
      via internal CC 102) and `vintage` (per-note random detune, wow/flutter,
      HF wear). Audited: nothing else in this repo models noise or jitter.
- [ ] SappLink table: 12 → 13 mappings, `clean` CC 3, `vintage` 21 → 12.
      Re-vendor `tests/data/sapplink-manifest.json` from sapptune verbatim.
- [ ] Plugin: `clean` APVTS parameter appended LAST (after `preset`), CLEAN knob
      in the CHARACTER panel, `mechNoise` default 1.0 → 0.18.
- [ ] Factory presets (plugin + CLI): nothing ships at 1.0; high values pulled
      toward the new default.
- [ ] CLI `clean` param + `mech_noise` default 0.18; docs tables.
- [ ] Tests: clean=1 silences mech noise, clean=0 is sample-identical,
      clean scaling ≡ pre-scaled params, default 0.18, CC 12 yes / CC 21 no,
      CC 3 → clean. State round-trip via `SappKeysUiShot --presettest`.
- [ ] `-DSAPPKEYS_BUILD_PLUGIN=ON` build of tests AND VST3; `./verify.sh`.
- [ ] Version 0.9.0 in CMakeLists (the CI release guard reads it), CHANGELOG,
      CURRENT_STATE; commit + push; close #3 with SHAs. No tag.

Last shipped: v0.8.0 (2026-08-09) — issues #1 + #2. Every async instrument
load now gates note-ons (suppress-until-installed; see DECISIONS 2026-08-09),
`libraryReady` host parameter added (non-APVTS, non-automatable, appended
last), build identity in the `SappKeys-build` / `SappKeys-audio-source` log
lines, verify.sh postmortem guards (plugin-cache OFF fails, stale installed
VST3 warns loudly). Tests 49/49, auval 18-param pass, both guard paths
exercised. Release built by the Windows runner off the v0.8.0 tag.
