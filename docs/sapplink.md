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

The full CC table is in [agent_api.md](agent_api.md#params).
