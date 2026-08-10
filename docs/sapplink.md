# SappLink — sappkeys

<!-- UPDATE WHEN: the CC map changes (three files must move together) -->

sappkeys implements SappLink v1 CC-in
(see `~/apps/sapptune/sapplink/PROTOCOL.md`).

- **Source of truth:** `~/apps/sapptune/sapplink/manifests/sappkeys.json`.
- **Vendored copy:** `tests/data/sapplink-manifest.json` — the unit test in
  `tests/unit/test_sapplink.cpp` fails if it drifts from the table in
  `src/core/SappLinkCCMap.cpp`.
- **Plugin path:** mapped CCs slew the APVTS parameter through the same
  normalized path host automation uses (no zipper). Proof:
  `SappKeysUiShot --cctest`.
- **Offline path:** `renderKeys()` applies mapped CCs to `KeysParams`
  mid-render.

Engine-native (never in the manifest): CC 1 dynamics, CC 11 expression,
CC 64 sustain. CC 102 is reserved as the internal mechanical-noise gain lane;
external CC 102 events are dropped.

**CC 3 = `clean`, suite-reserved** (sapptune #30). Every sapp* plugin answers
CC 3 with the same meaning: scale every modeled imperfection by (1 − clean).
Here that is `mechNoise` and `vintage`; the engine applies it in one place
(`applyClean()` in `src/core/KeysEngine.h`) so no source can be missed.

**`vintage` is on CC 12, not CC 21** (sappkeys #3). CC 21 is `eqAirGain` in
Sapprack, Sappmaster and Sappedal — and a CC reaches every plugin in the chain,
so an air-EQ setpoint used to age the piano as a side effect, with no way for a
host `--set` to win against the re-firing clip. CC 12 is free suite-wide.

The full CC table is in [agent_api.md](agent_api.md#params).
