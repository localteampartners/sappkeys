#!/usr/bin/env bash
# verify.sh — fast feedback loop for sappkeys.
# Builds core+CLI+tests (plugin skipped for speed unless build/ already has it)
# and runs both this repo's tests and a CLI smoke check.

set -e
cd "$(dirname "$0")"

echo "▶ configure"
if [ ! -d build ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSAPPKEYS_BUILD_PLUGIN=OFF > /dev/null
fi

echo "▶ build"
cmake --build build -j8 --target SappKeysTests sappkeys-cli 2>&1 | grep -E "error|FAILED" && exit 1 || true

echo "▶ tests"
./build/SappKeysTests --reporter compact | tail -2

echo "▶ cli smoke"
./build/sappkeys params > /dev/null
./build/sappkeys presets > /dev/null
./build/sappkeys inspect --diagnostic | head -c 120; echo " ..."

echo "✓ verify passed"
