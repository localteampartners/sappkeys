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
  -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/apps/sappsynth/build/_deps/juce-src
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
