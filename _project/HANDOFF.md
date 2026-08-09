# Handoff — sappkeys

<!-- UPDATE WHEN: starting/finishing multi-step work, or before ending a session mid-task -->

Work in flight: sapptune#17 output-safety fix — **code complete, uncommitted**.

Done (see CHANGELOG 2026-08-08 and ARCHITECTURE "Output safety"):
real gain-reduction Safety Limiter replacing the tanh soft-clipper, an
unconditional non-finite/±1 output guard, a MIDI-flood cap that panics instead
of dropping note-offs, and an 8 ms note-off guard around the SappSounds
steal-fade race. `tests/unit/test_safety.cpp` is new; verify.sh, the full
Catch2 suite and `auval -v aumu Skys Ltpr` all pass; VST3 + AU built Release.

Not done, deliberately: the root cause of the stuck notes is in SappSounds
(`PlaybackEngine::triggerRegion` gives a stolen voice a pending start, and
`noteOff` only sees `State::Active` voices, so a note-off inside the 3 ms steal
fade is lost). SappKeys works around it; SappSynth / SappOrchestra / SappKit /
SappChoir still have it. Fix it in sappsounds and the workaround can go.

v0.3.0 shipped 2026-08-07: in-plugin updater (+ GET SOUNDS from
CURRENT_STATE.md + TODO.md.
