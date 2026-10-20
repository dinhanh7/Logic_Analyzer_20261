#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RP2350_ROOT"
mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iprotocol tests/test_protocol.c -o build/tests/test_protocol
build/tests/test_protocol
python3 tests/test_integration.py
