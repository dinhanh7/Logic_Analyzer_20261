#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
if [[ ! -x "$RP2350_ROOT/.local/bin/pulseview" ]]; then echo 'Run scripts/build.sh host first.' >&2; exit 1; fi
# With no argument use the connected Pico. Other PulseView options can follow it.
conn="${1:-/dev/ttyACM0}"
if [[ $# -gt 0 ]]; then shift; fi
if [[ ! -r "$conn" || ! -w "$conn" ]]; then
    echo "Cannot access $conn. See scripts/doctor.sh and README.md (serial permissions)." >&2; exit 1
fi
exec "$RP2350_ROOT/.local/bin/pulseview" -d "rp2350-la:conn=$conn" "$@"
