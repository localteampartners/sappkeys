#!/usr/bin/env bash
# verify.sh — fast feedback loop for sappkeys.
# Builds core+CLI+tests (plugin skipped for speed unless build/ already has it)
# and runs both this repo's tests and a CLI smoke check.
#
# Postmortem guards (issue #1): green tests here say NOTHING about the
# artifact users load. Three waves of v0.6–v0.7 safety fixes once verified
# green while ~/Library/Audio/Plug-Ins/VST3/SappKeys.vst3 stayed at its
# pre-safety build, because every session configured SAPPKEYS_BUILD_PLUGIN=OFF
# and the install step never ran. So this script now also:
#   * FAILS unless some build dir (build/ or build-plugin/) is configured
#     with SAPPKEYS_BUILD_PLUGIN=ON — there must be a way for fixes to reach
#     the installed plugin;
#   * loudly warns when the installed VST3 binary is OLDER than any file in
#     src/ (the plugin has not been rebuilt since the change on disk).

set -e
cd "$(dirname "$0")"

echo "▶ configure"
if [ ! -d build ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSAPPKEYS_BUILD_PLUGIN=OFF > /dev/null
fi

echo "▶ build"
cmake --build build -j8 --target SappKeysTests sappkeys-cli 2>&1 | grep -E "error|FAILED" && exit 1 || true

echo "▶ tests"
# Not `SappKeysTests | tail`: a pipeline's status is the LAST command's, so a
# failing suite exited 0 through `tail` and verify.sh still said "passed" —
# and the compact reporter's trailing blank lines hid the summary anyway.
# Capture first (set -e sees the real status), then print the verdict line.
test_output=$(./build/SappKeysTests --reporter compact)
echo "$test_output" | grep -E "All tests passed|test cases:" || echo "$test_output" | tail -3

echo "▶ cli smoke"
./build/sappkeys params > /dev/null
./build/sappkeys presets > /dev/null
./build/sappkeys inspect --diagnostic | head -c 120; echo " ..."

echo "▶ install guard (postmortem, issue #1)"
plugin_on=""
for cache in build/CMakeCache.txt build-plugin/CMakeCache.txt; do
  if [ -f "$cache" ] && grep -q '^SAPPKEYS_BUILD_PLUGIN:BOOL=ON$' "$cache"; then
    plugin_on="$cache"
  fi
done
if [ -z "$plugin_on" ]; then
  echo ""
  echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  echo "!!  SAPPKEYS_BUILD_PLUGIN=OFF in every build dir.                   !!"
  echo "!!  Nothing verified here can reach the installed plugin — this is  !!"
  echo "!!  exactly how the v0.6–v0.7 fixes never shipped (issue #1). Fix:  !!"
  echo "!!    cmake -S . -B build-plugin -DSAPPKEYS_BUILD_PLUGIN=ON         !!"
  echo "!!    cmake --build build-plugin --target SappKeysPlugin_VST3 -j8   !!"
  echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  echo ""
  exit 1
fi
echo "  plugin build configured ON in: $plugin_on"

if [ "$(uname)" = "Darwin" ]; then
  vst3_bin="$HOME/Library/Audio/Plug-Ins/VST3/SappKeys.vst3/Contents/MacOS/SappKeys"
  # Compare against source FILE mtimes, not the newest commit: building and
  # then committing is the normal order, so a commit-time comparison fires
  # after every single commit and trains everyone to ignore the banner. What
  # matters is whether the installed binary is older than the code on disk.
  newest_src=$(find src CMakeLists.txt -type f -newer "$vst3_bin" -print -quit 2>/dev/null)
  stale=""
  if [ ! -f "$vst3_bin" ]; then
    stale="no VST3 installed at ~/Library/Audio/Plug-Ins/VST3/SappKeys.vst3"
  elif [ -n "$newest_src" ]; then
    bin_mtime=$(stat -f %m "$vst3_bin")
    stale="installed VST3 built $(date -r "$bin_mtime" '+%Y-%m-%d %H:%M'), but $newest_src is newer"
  fi
  if [ -n "$stale" ]; then
    echo ""
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "!!  STALE INSTALLED PLUGIN — the DAW is NOT playing this code.      !!"
    echo "!!  $stale"
    echo "!!  Rebuild + install:                                              !!"
    echo "!!    cmake --build build-plugin --target SappKeysPlugin_VST3 -j8   !!"
    echo "!!  (COPY_PLUGIN_AFTER_BUILD installs it; then rescan in the DAW.)  !!"
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo ""
  else
    echo "  installed VST3 is newer than every file in src/"
  fi
fi

echo "✓ verify passed"
