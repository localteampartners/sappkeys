# Handoff — sappkeys

<!-- UPDATE WHEN: starting/finishing multi-step work, or before ending a session mid-task -->

Work in flight: none.

Last shipped: v0.10.0 (2026-08-11) — issue #4. `libraryReady` no longer reports
a library that is not in. A program change or preset move queued for the timer
now counts as a load window (`changePending()` in `SappKeysProcessor`) for both
the flag and the note-on gate, and every entry point that can begin a load
clears the flag synchronously on the calling thread. Before this, the 1.5 s
fresh-insert grace window armed `StartupGate` under a queued program change and
the flag went 1 over the construction diagnostic — the cause of the 40–60 s of
digital silence at the head of every `wanderer-piano` take. New
`tools/headless/` station harness (`sappkeys-headless`), 26 checks, run by
CTest and verify.sh, with a fixture at `tests/data/keys-headless/` reached via
the new `$SAPP_SAMPLES_ROOT` override. Tests 58/58 + 26 headless; auval PASS.
The same MIDI-program-change hole was fixed in sapporchestra v0.10.0, sappchoir
v0.8.0 and sappkit v0.8.0.

**Open loop:** nothing here is tagged. v0.9.0 (d3fcfae) and now v0.10.0 are
committed and pushed with no tag; tagging and releasing is driven separately.
The CI guard compares the tag against `project(SappKeys VERSION 0.10.0)` in
CMakeLists.txt, which is already correct.
