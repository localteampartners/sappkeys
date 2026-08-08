# TODO — sappkeys

<!-- UPDATE WHEN: tasks are added, started, or finished -->

## Next

- [ ] One manual UPDATE-button click on a Windows machine (rename-trick
      .vst3 swap is untested on real Windows; macOS path verified).
- [ ] Smoke-test VST3/AU in a third-party host (Logic / Reaper).
- [ ] Pedal down/up noise samples: needs `on_loccN`/`on_hiccN` trigger
      support in SappSounds; then extend `applyKeysPolicy` to tag them.
- [ ] Half-pedal: map CC64 1–63 to partial damper (shorter resonance T60,
      faster release fade).

## Later

- [ ] Plugin-side preset browser mirroring the CLI presets.
- [ ] Release-sample `rt_decay` support in SappSounds (quieter thumps after
      long notes).
- [ ] A tine-EP sample set with release noises (mech knob currently no-ops on
      FM-Piano1 — it has no release samples).
- [ ] Sample-rate-aware resonance release coefficient.
