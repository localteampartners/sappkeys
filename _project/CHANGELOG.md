# Changelog — sappkeys

<!-- UPDATE WHEN: anything meaningful ships -->

## 2026-08-11 — v0.10.0: `libraryReady` stops lying about a queued program (#4)

- **The fault.** `setCurrentProgram()` only QUEUES the program; the load runs
  on the 30 Hz timer. `libraryReady` mirrored `StartupGate::armed()`, and the
  fresh-insert grace window arms the gate 1.5 s after construction — so on a
  station that set a program at instantiation the flag went 1 at ~1.55 s with
  nothing installed but the construction-default diagnostic, while the program
  change still sat in the queue. sappradio polled it, stopped waiting, and
  rendered its opening bars into the load that followed: `wanderer-piano`
  carried 934 note events from 0.00 s and came out digitally silent
  (mean = peak = −91 dB) for the first 40–60 s, the window moving between
  takes because it tracks a wall-clock load, not the music.
- **The fix.** A queued change is a load window. `changePending()` (a program
  change or preset move waiting for the timer) now ANDs into BOTH the
  `libraryReady` readout and the note-on gate, and every entry point that can
  begin a load clears the flag SYNCHRONOUSLY, on the calling thread, before
  anything is queued: `setCurrentProgram()`, the MIDI program-change branch of
  `processBlock()`, the `preset` parameter listener, and (already) the SFZ /
  user-preset / diagnostic / state-restore paths. Deferring the clear to the
  timer only moves the race. The term deliberately lives in the processor, not
  in `StartupGate`: the timer's program-hold logic asks `armed()` to decide
  when a held program may finally be applied, so a pending request that
  disarmed the gate would hold itself forever.
- Measured, `sappkeys-headless selftest`, program 1 "Intimate Grand":
  before — `ready@1.51s`, installed `(diagnostic)`, first 120 ms of the render
  at −30.6 dBFS (the diagnostic instrument, which is also the likeliest
  explanation for the "terrible sappkeys sound" heard partway through
  wanderer-piano); after — `ready@1.57s`, installed
  `SalamanderGrandPiano-V3.sfz`, head at −8.3 dBFS from the first block.
- **New: `tools/headless/` station harness** (`sappkeys-headless`), the target
  this repo was missing. `selftest` is the issue-#4 regression — 26 checks
  across the host program API, the `preset` parameter, MIDI program change and
  state restore, asserting that the library the host asked for is installed at
  the instant the flag first reads 1, that the flag drops synchronously on a
  mid-session swap, and that a render started when the flag reads 1 is not
  silent at the head. `render` produces one station-style take. Wired into
  CTest and `verify.sh`; it runs the real plugin processor, so it only exists
  when the plugin target is built (issue #1's postmortem).
- `$SAPP_SAMPLES_ROOT` now overrides the persisted samples root (same name and
  semantics as sappkit), so the regression resolves factory programs against
  `tests/data/keys-headless/` without a sample library installed and without
  writing to the user's shared Sapp settings file.
- verify.sh builds the plugin and the harness and runs the selftest. Tests
  58/58 plus 26 headless checks; auval passes.
- Same fault audited in the three siblings: the parameter and host-program-API
  paths were already correct there, but the MIDI program-change branch had the
  same hole (the audio thread queues the select; readiness was only
  republished on the loader thread ~5 ms later). Fixed in sapporchestra
  v0.10.0, sappchoir v0.8.0 and sappkit v0.8.0.
- Not tagged here — the release is driven separately.

## 2026-08-09 — v0.9.0: `clean` (CC 3), quieter Mechanics, vintage off CC 21 (#3)

- **New host parameter `clean`** ("Clean", 0..1, default 0, SappLink CC 3) —
  the suite-wide imperfection master (sapptune #30). It scales EVERY modeled
  imperfection by (1 − clean): `mechNoise` (hammer/key/damper/pedal noise via
  the reserved internal CC 102 lane) and `vintage` (per-note random detune,
  wow & flutter, softened-HF wear). At 0 the sound is exactly what it was —
  a multiply by 1.0f is exact, so renders stay sample-identical; at 1 the
  plugin emits no modeled noise, wear or jitter. Applied in ONE place
  (`applyClean()`, `src/core/KeysEngine.h`) so a future imperfection source
  cannot silently escape it. Appended LAST in the APVTS layout: every existing
  sound parameter keeps its index, and a session saved before `clean` existed
  restores at 0 with all 16 other parameters bit-identical (proved by a new
  `--presettest` step that strips the `clean` node from the state XML).
  Sympathetic resonance, room and drive are deliberately NOT scaled — they are
  the instrument, not its imperfections. New CLEAN knob in the CHARACTER panel.
- **Mechanics default 1.0 → 0.18.** Shipping at full scale meant every
  instance broadcast maximum mechanism noise; on a 24/7 station several
  modeled instruments stacked into the "grain" listeners reported (~20 dB
  above a clean sampler chain above 6 kHz). Factory presets pulled in line
  too — nothing ships at 1.0 (plugin bank: Grand Concert 0.7→0.25, EP Mark I
  0.4→0.2, EP Dyno 0.3→0.18, EP Crunchy 0.35→0.2, Una Corda Soft 0.85→0.5;
  CLI: `felt` 1.0→0.55, `ep-tine` 0.7→0.25).
- **`vintage` moved from CC 21 to CC 12.** CC 21 is `eqAirGain` in Sapprack,
  Sappmaster and Sappedal; CCs reach every plugin in a chain, so sapptune's
  air-EQ setpoints also aged the piano — and a host `--set` on Vintage could
  never win against the re-firing clip. CC 12 is free across every manifest.
  Clips written against the old map must be re-rendered.
- 9 new tests (58 total, was 49). Measured: with Mechanics at 1.0, `clean` 1
  drops the release-window level 36 dB and lands bit-identical to Mechanics 0.
  auval passes with 19 parameters; `--cctest` now also proves CC 3 → clean,
  CC 12 → vintage and CC 21 → *no* vintage change through the plugin path.
- **verify.sh: a failing test suite could not fail the script.** `SappKeysTests
  | tail -2` gave the pipeline `tail`'s exit status (0), and the compact
  reporter's trailing blank lines hid the summary line anyway. Now captured
  first, so `set -e` sees the real status and the verdict prints.
- Not tagged: the GitHub build host is down (self-hosted runner being
  re-set-up), so v0.9.0 is committed and pushed and the release fires later.

## 2026-08-09 — v0.8.0: load-window gating + postmortem guards (#1, #2)

- **Every async instrument load now owns a note-on gate window** (issue #2).
  v0.7.0's `StartupGate` only covered the state-restore path; a mid-session
  program change / preset choice / user preset / Sounds-panel SFZ pick still
  raced its own async load, and notes arriving in those seconds played the
  OUTGOING (or diagnostic) instrument. Documented choice: note-ons are
  SUPPRESSED (silence) until the new instrument is installed — consistent
  with the startup behavior; already-sounding notes are not cut (the engine's
  swap steal-fade retires them at install). Failure policy: a failed
  mid-session load re-arms onto the still-installed real instrument (a
  corrupt pick must not brick the session); a failed restore over the
  construction diagnostic stays silent (unchanged #21 semantics).
- **`libraryReady` host parameter** (issue #2): readable, non-automatable
  bool mirroring the gate, appended last with a new stable ID, deliberately
  OUTSIDE the APVTS so state save/load never captures or restores it.
  Headless hosts (sappradio) can poll readiness instead of a blind settle
  window. User-preset capture skips non-automatable parameters.
- **verify.sh postmortem guards** (issue #1): fails (loud banner, exit 1)
  when no build dir is configured with `SAPPKEYS_BUILD_PLUGIN=ON`; prints an
  unmissable staleness banner when the installed VST3 binary is older than
  the newest commit touching `src/`. Still <1 min warm.
- **Build identity in host logs** (issue #1): `SappKeys-build: version=X.Y.Z`
  at plugin construction, and `build=X.Y.Z` on every `SappKeys-audio-source`
  line (`SAPPKEYS_VERSION` compile definition from the CMake project
  version — the same single source the CI version-vs-tag guard checks).
- 6 new tests (49 total). auval passes with 18 parameters.

## 2026-08-09 — instrument-state safety (sapptune#21)

- **Pre-state note-on gate (`StartupGate`).** The constructor installs the
  Diagnostic Orchestra as a placeholder; stray MIDI arriving before
  `setStateInformation`'s async SFZ load finished could sound it — the
  "altogether default sound" of #21. Note-ons are now suppressed until the
  restore's instrument is installed (or a 1.5 s fresh-insert grace passes);
  MIDI program changes are deferred (not dropped) over the same window.
  Suppressions are logged: grep Live's Log.txt for `SappKeys-midi-gate`.
- **Audio-source identity logging.** When a voice batch starts from silence
  the timer logs which install produced it, throttled:
  `SappKeys-audio-source: instrument="<sfz path | DIAGNOSTIC(reason)>"
  gen=N armed=0|1 voices=V`. A recurrence now names its own cause.
- **State restore no longer re-applies the `preset` choice.** `replaceState`
  fired the preset parameter listener, which re-applied the preset over the
  restored state (clobbering saved tweaks, swapping to the preset's library
  instead of the saved `sfzPath`). Guarded.
- **Deferred snapshot retirement.** `collectRetired()` now runs only after the
  audio thread has rendered ≥0.5 s past the swap (`KeysEngine::framesRendered`),
  so a steal-fading voice can never read a freed instrument snapshot when the
  host suspends audio around a load.
- 7 new tests (43 total): StartupGate policy, pre-state silence, swap-under-
  load fade/rebind, rapid-swap churn. auval + uishot --cctest/--presettest pass.

## 2026-08-08 — output safety (sapptune#17)

- **Safety Limiter is now a limiter.** It was `tanh()` on the output: peak
  stayed at 0 dBFS but a dense burst came out as a full-scale square wave.
  64 simultaneous notes at velocity 127 (Salamander) measured **-0.00 dBFS
  peak / -4.32 dBFS burst RMS before, -1.00 dBFS / -12.06 dBFS after**. It is
  now block-lookahead gain reduction to -1 dBFS: no added latency, no
  clipping, loud material turned down instead of squared off. The default was
  already ON and is unchanged, so nothing recalls differently — but the tone
  above roughly -6 dBFS does change, which is the point of the fix.
- **Unconditional output guard.** Non-finite samples become 0 and every
  filter/delay state is scrubbed so a NaN can't park inside an IIR; output is
  clamped to ±1 even with the limiter switched off (that case measured
  +25 dBFS with Master at +12 dB before).
- **MIDI floods no longer strand notes on.** The per-block event buffer was a
  fixed 300 and truncated silently — and the events that get cut are the
  note-offs, so a burst left notes stuck on at full velocity forever. The cap
  is now `kMaxBlockEvents` (1024, preallocated) and overflow ends every voice
  instead of keeping the note-ons.
- **Note-off guard.** SappSounds starts a *stolen* voice only when its 3 ms
  steal fade finishes; a note-off arriving inside that window found no active
  voice and was dropped, so the note sounded forever. Reproducible from ~46
  simultaneous notes whose note-offs land in the same block. KeysEngine now
  holds a note-off back until its note-on is 8 ms old. The underlying bug is
  in SappSounds' `PlaybackEngine` (`triggerRegion` pending-start vs
  `noteOff`) and affects every instrument on that engine — still worth fixing
  there.
- No allocation on the audio thread: the plugin's event scratch is sized to
  the cap, and `std::stable_sort` now runs only when the buffer isn't already
  ordered (a MidiBuffer always is).
- New `tests/unit/test_safety.cpp` covers all of the above.

## 2026-08-09 — v0.5.1

- Fixed the plugin version, which was still 0.3.0 while releases had moved
  on to v0.5.0. The in-plugin updater compares the running build's version
  against the latest tag, so a v0.5.0 install reported itself as 0.3.0 and
  kept re-offering the same update after installing it. The binary now
  reports its real version; RUNBOOK already requires bumping
  `project(SappKeys VERSION ...)` with every release tag.

## 2026-08-08 — user presets

- SappLink user presets (sapptune/sapplink/PRESETS.md): save the current
  sound to `<Documents>/SappSounds/presets/sappkeys/<name>.json` as
  normalised values, load it by name from any instance.
  `src/plugin/UserPresets.{h,cpp}` is the suite-shared implementation,
  copied verbatim from sappsynth.
- New `preset` APVTS parameter (AudioParameterChoice, added LAST in the
  layout so no existing parameter index moves, no CC): factory bank in
  program order, then the user presets found at construction. Host- and
  SappLink-automatable; applied on the message thread by the existing
  30 Hz timer.
- Saved presets record the loaded SFZ library in the file's `sfz` field
  and restore it on load when the path still resolves.
- Editor footer: PRESET chooser (rescans on open, user entries marked
  "(user)") + SAVE with an async name dialog; outcome shown in the status
  line.
- Manifest gained a top-level `hostParameters` entry for `preset` (not
  `parameters` — it carries no CC).
- `SappKeysUiShot --presettest`: headless round-trip proof (capture ->
  disk -> fresh processor, max |diff| 0) plus `preset`-parameter, MIDI
  program-change and host-state regressions. `--cctest` now waits for the
  instrument load instead of a fixed 2.5 s (it measured silence on a busy
  machine).

## 2026-08-07 — v0.3.0
- In-plugin UPDATE button: daily GitHub release check (click the version
  number to check on demand); one click downloads and installs the new
  build (macOS: plug-in folders + quarantine cleared; Windows: loaded
  .vst3 swapped via rename), standalone relaunches itself on macOS.
- Plugin version now tracks release tags (0.3.0).

## 2026-08-06 (later)

- GET SOUNDS panel: in-plugin library downloads + instrument browser (ported
  from sapporchestra). Registry: Salamander Grand Piano (707 MB, CC-BY,
  tar.gz), FreePats Upright Piano KW / FM Piano 1 / Old Piano FB (CC0, zips).
  Shared Sapp samples root (~/Samples, persisted in Application Support/Sapp).
  UiShot gained `--sounds`.

## 2026-08-06

- v0.1.0: initial release.
- Core keys engine (touch curve, una corda, lid, sympathetic resonance,
  mech-noise policy, vintage/drive, small room) on SappSounds.
- `sappkeys` agent CLI (inspect/validate/params/presets/scan/render, JSON,
  seeded deterministic).
- JUCE 8.0.15 plugin (Standalone/VST3/AU) with ivory/ebony editor: velocity-
  curve display, pedal lamps, 88-key keyboard. UiShot + --cctest.
- SappLink manifest (12 params; CC1/11/64 native) + vendored drift guard.
- 28 Catch2 tests. Demo: Gymnopédie No. 1 through Salamander + FM EP.
- Added fm-piano1 / upright-piano / old-piano-fb (all CC0) to sappsounds
  fetch-library.sh.
