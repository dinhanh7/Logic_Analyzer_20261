#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RP2350_ROOT"
if git -C third_party/pulseview apply --check "$RP2350_ROOT/patches/pulseview-demo.patch" 2>/dev/null; then
    git -C third_party/pulseview apply "$RP2350_ROOT/patches/pulseview-demo.patch"
else
    git -C third_party/pulseview apply --reverse --check "$RP2350_ROOT/patches/pulseview-demo.patch"
fi
cmake --build build/firmware-pico2 -j4
cmake --build build/pulseview -j4
