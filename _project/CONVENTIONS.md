# Conventions — sappkeys

<!-- UPDATE WHEN: a workflow rule or gotcha is learned -->

- **Parameter IDs are contracts** (APVTS ids = SappLink manifest ids = CLI
  `id` fields). Never rename or renumber; add new ones instead.
- **SappLink changes are three-file changes**: sapptune manifest +
  `tests/data/sapplink-manifest.json` + `src/core/SappLinkCCMap.cpp`. The
  drift test fails if any one moves alone.
- **Reserved CCs**: 1, 11, 64 engine-native; 102 internal (mech noise).
  Don't map them in SappLink. CC 3 is the suite-wide `clean` lane — the same
  meaning in every sapp* plugin; never repurpose it.
- **Check every manifest before picking a CC** — all of
  `~/apps/sapptune/sapplink/manifests/`, not just this repo's. A CC reaches
  every plugin in the chain: `vintage` on CC 21 collided with Sapprack's
  `eqAirGain` and shipped that way for months (issue #3).
- **New parameters are appended LAST** in `makeLayout()`, never inserted:
  indices are automation lanes, and a session saved before the parameter
  existed must restore on its default (`clean` → 0 = old behavior).
- **Defaults are a broadcast decision.** Several modeled instruments in one
  unattended mix stack their imperfection; ship character on, but low
  (Mechanics 0.18), never at full scale.
- **No JUCE in `src/core/`** — the CLI and tests must link without it.
- **verify.sh is the loop** (core+CLI+tests, plugin off). Full plugin builds
  go in `build-plugin/` so the fast loop's `build/` stays lean.
- Sibling repos: engine fixes belong in `../sappsounds` (own repo/session);
  keep this repo product-policy only.
- Samples stay in `~/Samples/` (fetch-library.sh); WAVs in `demo/` are
  gitignored, the demo MIDI + generator script are committed.
- Strict warnings (`sappkeys_set_warnings`) on all our targets; third-party
  noise from sappsounds' dr_flac is expected and not ours to fix.
