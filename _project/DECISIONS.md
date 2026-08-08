# Decisions — sappkeys

<!-- UPDATE WHEN: a non-obvious design choice is made -->

## 2026-08-06 — Mechanical noises via an internal reserved CC (102)

Release-sample level had to be product-controllable without touching sample
data or forking SappSounds. `applyKeysPolicy` injects `gain_cc102 = -60 dB`
into every release-trigger region at load; the engine drives CC 102 from the
`mechNoise` param and drops external CC 102. Chosen over rebuilding
instrument snapshots per knob move (expensive: samples are decoded in RAM)
and over a SappSounds API change (engine stays product-neutral).

## 2026-08-06 — Una corda is a SappLink parameter on CC 67, not engine-native

CC 67 is the MMA soft pedal, but routing it through the SappLink mapping (→
`unaCorda` APVTS param with slew) gives one code path for pedal, host
automation, and sapptune, and keeps the reserved-native set minimal
(1/11/64/102). The engine reads the param; it never parses CC 67 itself.

## 2026-08-06 — Sympathetic resonance = tuned comb bank, not a sample layer

A quiet looped resonance sample layer would need library-specific content.
Twelve feedback combs (claimed by held/played notes while the pedal is down,
±5 ct deterministic detune, in-loop damping + DC blocker) work on any
library, cost ~nothing, and die fast on pedal-up. The DC blocker matters: a
feedback comb also resonates at 0 Hz, and in-loop damping makes that mode
decay slowest.

## 2026-08-06 — Small-room FDN (6 lines, ≤2.5 s), not the orchestra hall

Pianos sit in rooms. Reusing sapporchestra's hall would blur the product
line; the 6-line FDN with short mutually-prime delays and light modulation
stays intimate and CPU-cheap. Anyone wanting a hall can render dry and reverb
downstream.

## 2026-08-06 — Same architecture as sapporchestra, deliberately

Core policy layer (no JUCE) + thin plugin + agent CLI + vendored-manifest
drift test. Familiarity across the suite beats novelty; diffs between the two
products are then purely product policy.

## 2026-08-06 — JUCE 8.0.15 pinned, checkout shared with sappsynth

Same tag as the rest of the suite; local configure points
FETCHCONTENT_SOURCE_DIR_JUCE at sappsynth's checkout to avoid a second 300 MB
clone. CI/others fall back to a normal FetchContent download.

## 2026-08-07 — Self-update via GitHub releases, versioned by the CMake project

The plugin updates itself from the repo's *latest GitHub release* rather
than a separate update feed: CI already attaches Windows-x64 and
macOS-universal zips to every tag, so the release IS the feed. The
installed version is `JucePlugin_VersionString`, which JUCE derives from
`project(SappKeys VERSION ...)` — therefore the CMake version MUST be
bumped with every release tag (RUNBOOK rule) or the updater goes blind.
Daily check throttled through the shared Sapp settings file (one file for
the whole product family, per-product key `lastUpdateCheck-sappkeys`).
Windows can't overwrite a loaded DLL but can rename it: old .vst3 is
parked as `.old-<tag>` and the new one copied in, with rollback on failure.
