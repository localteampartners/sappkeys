# Environment — sappkeys

<!-- UPDATE WHEN: an env var or external requirement is added/removed -->

No runtime env vars and no secrets. Build-time knobs (CMake):

| Variable | Default | Purpose |
|---|---|---|
| `SAPPSOUNDS_DIR` | `../sappsounds` | local SappSounds checkout; FetchContent fallback if absent |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | (unset) | reuse an existing JUCE 8.0.15 checkout (e.g. sappsynth's) |
| `SAPPKEYS_BUILD_PLUGIN/TESTS/CLI` | ON | target toggles |

Toolchain: macOS, Xcode CLT clang (C++20), CMake ≥ 3.24, python3 for the demo
script.
