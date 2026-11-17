#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RP2350_ROOT"
# Propagate the same host-library environment to CLI and PulseView.
exec python3 tools/demo.py "$@"
