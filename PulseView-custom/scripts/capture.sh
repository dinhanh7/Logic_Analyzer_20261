#!/usr/bin/env bash
# Publish a .sr only after the driver confirms count AND capture CRC.
set -euo pipefail
source "$(dirname "$0")/env.sh"
conn="${1:-/dev/ttyACM0}"
output="${2:-capture.sr}"
rate="${3:-1m}"
count="${4:-100000}"
if [[ -e "$output" ]]; then echo "Output exists: $output" >&2; exit 1; fi
work="$(mktemp -d "$(dirname "$output")/.lar2-capture.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT
log="$output.log"
if "$RP2350_ROOT/.local/bin/sigrok-cli" -l 3 -d "rp2350-la:conn=$conn" \
    --config "samplerate=$rate" --samples "$count" -o "$work/capture.sr" 2>"$log" && \
    rg -q 'Capture verified:' "$log" && [[ -s "$work/capture.sr" ]]; then
    mv -n -- "$work/capture.sr" "$output"
    echo "Verified capture: $output (log: $log)"
else
    cat "$log" >&2
    echo 'Capture incomplete or invalid; no output file published.' >&2
    exit 1
fi
