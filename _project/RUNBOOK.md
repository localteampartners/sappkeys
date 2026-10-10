# Runbook — sappkeys

<!-- UPDATE WHEN: build, run, or release steps change -->

## Fast loop (core + CLI + tests, no JUCE)

```bash
./verify.sh
```

## Full plugin build (Standalone / VST3 / AU + UiShot)

```bash
cmake -S . -B build-plugin -DCMAKE_BUILD_TYPE=Release \
  -DSAPPKEYS_BUILD_TESTS=OFF -DSAPPKEYS_BUILD_CLI=OFF \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/apps/sappaudio/sappsynth/build/_deps/juce-src
cmake --build build-plugin -j8 --target SappKeysPlugin_Standalone SappKeysUiShot
open build-plugin/SappKeysPlugin_artefacts/Release/Standalone/SappKeys.app
```

Drop `-DFETCHCONTENT_SOURCE_DIR_JUCE=...` to let CMake fetch JUCE 8.0.15
itself (first configure downloads ~300 MB). `juce_enable_copy_plugin_step`
installs VST3/AU into the user plugin folders on build.

## UI screenshot / SappLink plugin proof

```bash
./build-plugin/SappKeysUiShot_artefacts/Release/SappKeysUiShot.app/Contents/MacOS/SappKeysUiShot /tmp/sappkeys-ui.png
./build-plugin/SappKeysUiShot_artefacts/Release/SappKeysUiShot.app/Contents/MacOS/SappKeysUiShot --cctest
```

## Samples

```bash
~/apps/sappsounds/scripts/fetch-library.sh get salamander     # 707 MB grand
~/apps/sappsounds/scripts/fetch-library.sh get fm-piano1      # 24 MB FM EP
~/apps/sappsounds/scripts/fetch-library.sh get upright-piano  # 34 MB upright
~/apps/sappsounds/scripts/fetch-library.sh get old-piano-fb   # 39 MB old piano
```

Samples live in `~/Samples/`, never in git.

## Render consistency (same MIDI, same piano)

```bash
build-plugin/SappKeysHeadless_artefacts/Release/sappkeys-headless midi \
  --midi <take>/song.mid --channels 1,3 --program 9 --runs 8
```
Renders the song N times the way `sappradio render` does (program, settle on
`libraryReady`, 512-frame blocks, loop pumped every 16) and exits 1 if any run
differs from run 0 by more than 0.05. Not bit-exact by design (room LFO phase
depends on settle length). For heap over-reads, run the core tests under
ASan: `cmake -B build-asan -DSAPPKEYS_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Debug
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"` then
`ASAN_OPTIONS=detect_leaks=0 build-asan/SappKeysTests`.

## Demo render

```bash
python3 scripts/make_demo.py demo/gymnopedie.mid
./build/sappkeys render \
  --sfz ~/Samples/salamander/SalamanderGrandPiano-SFZ+FLAC-V3+20200602/SalamanderGrandPiano-V3+20200602.sfz \
  --midi demo/gymnopedie.mid --out demo/gymnopedie-salamander.wav \
  --preset concert-grand --param master_gain_db=6 --seed 20260806 --tail 5
```

## Rollback

Plain git: `git log`, `git revert <sha>`. No deploy target.

## Release rule (in-plugin updater)

Bump `project(SappKeys VERSION X.Y.Z)` in CMakeLists.txt to match every
release tag — the in-plugin updater compares JucePlugin_VersionString
against the latest GitHub tag, so the two MUST stay in sync.
