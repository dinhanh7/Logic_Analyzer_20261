#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RP2350_ROOT"
mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iprotocol tests/test_protocol.c -o build/tests/test_protocol
build/tests/test_protocol
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iprotocol tests/test_trigger.c -o build/tests/test_trigger
build/tests/test_trigger
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ifirmware -Iprotocol firmware/decoders.c tests/test_decode.c -o build/tests/test_decode
build/tests/test_decode
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ifirmware -Iprotocol firmware/font6x8.c tests/test_ui.c -o build/tests/test_ui
build/tests/test_ui
python3 tests/test_host_la.py
python3 tests/test_stream.py
# The panel itself has no unit test; the render harness compiles ui.c against
# stubs and checks the caption formatters, which are the only branchy part of
# the time ruler.
mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Wno-unused-function -Ifirmware -Iprotocol \
   tools/render_ui.c firmware/ui.c firmware/font6x8.c firmware/decoders.c \
   -o build/tests/render_ui
build/tests/render_ui --selftest
# The sigrok driver tests need the host stack built with scripts/build.sh host.
if [[ -x "$RP2350_ROOT/.local/bin/sigrok-cli" ]]; then
    python3 tests/test_integration.py
else
    echo 'sigrok-cli not built, skipping the driver integration tests' >&2
fi
