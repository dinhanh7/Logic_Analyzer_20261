#!/usr/bin/env bash
# Launch the custom PulseView build with our libsigrok (rp2350-la driver).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$ROOT/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$ROOT/.local/bin/pulseview" "$@"
