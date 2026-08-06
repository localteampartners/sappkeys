# Conventions — sappkeys

<!-- UPDATE WHEN: a workflow rule or gotcha is learned -->

- **Parameter IDs are contracts** (APVTS ids = SappLink manifest ids = CLI
  `id` fields). Never rename or renumber; add new ones instead.
- **SappLink changes are three-file changes**: sapptune manifest +
  `tests/data/sapplink-manifest.json` + `src/core/SappLinkCCMap.cpp`. The
  drift test fails if any one moves alone.
- **Reserved CCs**: 1, 11, 64 engine-native; 102 internal (mech noise).
  Don't map them in SappLink.
- **No JUCE in `src/core/`** — the CLI and tests must link without it.
- **verify.sh is the loop** (core+CLI+tests, plugin off). Full plugin builds
  go in `build-plugin/` so the fast loop's `build/` stays lean.
- Sibling repos: engine fixes belong in `../sappsounds` (own repo/session);
  keep this repo product-policy only.
- Samples stay in `~/Samples/` (fetch-library.sh); WAVs in `demo/` are
  gitignored, the demo MIDI + generator script are committed.
- Strict warnings (`sappkeys_set_warnings`) on all our targets; third-party
  noise from sappsounds' dr_flac is expected and not ours to fix.
