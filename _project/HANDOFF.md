# Handoff — sappkeys

<!-- UPDATE WHEN: starting/finishing multi-step work, or before ending a session mid-task -->

Work in flight: none.

Last shipped: v0.8.0 (2026-08-09) — issues #1 + #2. Every async instrument
load now gates note-ons (suppress-until-installed; see DECISIONS 2026-08-09),
`libraryReady` host parameter added (non-APVTS, non-automatable, appended
last), build identity in the `SappKeys-build` / `SappKeys-audio-source` log
lines, verify.sh postmortem guards (plugin-cache OFF fails, stale installed
VST3 warns loudly). Tests 49/49, auval 18-param pass, both guard paths
exercised. Release built by the Windows runner off the v0.8.0 tag.
