# Dependencies — sappkeys

<!-- UPDATE WHEN: an external service, API, or library dependency changes -->

## Code (no accounts, no paid services)

| Dependency | Version | How |
|---|---|---|
| SappSounds | sibling `main` | `add_subdirectory(../sappsounds)`; FetchContent from GitHub otherwise |
| JUCE | 8.0.15 (pinned) | FetchContent; local builds share sappsynth's checkout |
| Catch2 | v3.7.1 | FetchContent (tests only) |

## Sample libraries (fetched, never committed)

| Library | License | Fetch |
|---|---|---|
| Salamander Grand Piano V3 | CC-BY 3.0 | `fetch-library.sh get salamander` |
| FreePats FM-Piano1 (EP) | CC0 | `fetch-library.sh get fm-piano1` |
| FreePats Upright Piano KW | CC0 | `fetch-library.sh get upright-piano` |
| FreePats Old Piano FB | CC0 | `fetch-library.sh get old-piano-fb` |

`fetch-library.sh` lives in the sappsounds repo (`scripts/`).

## Cross-repo contracts

- `~/apps/sapptune/sapplink/manifests/sappkeys.json` — SappLink source of
  truth; vendored copy in `tests/data/` is drift-guarded by a unit test.
