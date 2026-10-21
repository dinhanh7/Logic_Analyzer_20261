#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RP2350_ROOT"
mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iprotocol tests/test_protocol.c -o build/tests/test_protocol
build/tests/test_protocol
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iprotocol tests/test_trigger.c -o build/tests/test_trigger
build/tests/test_trigger
python3 tests/test_host_la.py
# The sigrok driver tests need the host stack built with scripts/build.sh host.
if [[ -x "$RP2350_ROOT/.local/bin/sigrok-cli" ]]; then
    python3 tests/test_integration.py
else
    echo 'sigrok-cli not built, skipping the driver integration tests' >&2
fi
