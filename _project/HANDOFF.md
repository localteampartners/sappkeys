# Handoff — sappkeys

<!-- UPDATE WHEN: starting/finishing multi-step work, or before ending a session mid-task -->

Work in flight: none.

Last shipped: v0.9.0 (2026-08-09, commit d3fcfae) — issue #3. New host
parameter `clean` (0..1, default 0, SappLink CC 3, appended last) scales every
modeled imperfection by (1 − clean) via `applyClean()` in
`src/core/KeysEngine.h`: `mechNoise` and `vintage`, the complete audited list.
clean 0 is sample-identical to before. Mechanics default 1.0 → 0.18 and no
factory preset ships near full scale. `vintage` moved off CC 21 (Sapprack /
Sappmaster / Sappedal `eqAirGain`) onto CC 12; sapptune's manifest re-vendored
verbatim. Tests 58/58, auval 19 params, `--cctest` and `--presettest` PASS
(the latter now proves a pre-`clean` session restores at 0 with the other 16
parameters bit-identical). verify.sh no longer swallows a failing suite.

**Open loop:** v0.9.0 is committed and pushed but deliberately NOT tagged —
the GitHub build host is down and the self-hosted runner is being re-set-up.
Tag `v0.9.0` on d3fcfae when the runner is back; the CI guard compares the tag
against `project(SappKeys VERSION 0.9.0)` in CMakeLists.txt, which is already
correct.
