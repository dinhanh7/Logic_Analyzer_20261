#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
exec "$RP2350_ROOT/.local/bin/sigrok-cli" "$@"
