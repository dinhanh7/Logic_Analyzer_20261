#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RP2350_ROOT"
if git -C third_party/pulseview apply --check "$RP2350_ROOT/patches/pulseview-demo.patch" 2>/dev/null; then
    git -C third_party/pulseview apply "$RP2350_ROOT/patches/pulseview-demo.patch"
else
    git -C third_party/pulseview apply --reverse --check "$RP2350_ROOT/patches/pulseview-demo.patch"
fi
if git -C third_party/libsigrokdecode apply --check "$RP2350_ROOT/patches/libsigrokdecode-session-race.patch" 2>/dev/null; then
    git -C third_party/libsigrokdecode apply "$RP2350_ROOT/patches/libsigrokdecode-session-race.patch"
else
    git -C third_party/libsigrokdecode apply --reverse --check "$RP2350_ROOT/patches/libsigrokdecode-session-race.patch"
fi
python3 scripts/integrate.py
for component in libsigrok libsigrokdecode; do
    make -C "third_party/$component" -j4
    make -C "third_party/$component" install
done
cmake --build build/firmware-pico2 -j4
cmake --build build/pulseview -j4
