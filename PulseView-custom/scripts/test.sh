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
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ifirmware -Iprotocol firmware/autodetect.c firmware/decoders.c tests/test_autodetect.c -o build/tests/test_autodetect
build/tests/test_autodetect
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ifirmware -Iprotocol firmware/font6x8.c tests/test_ui.c -o build/tests/test_ui
build/tests/test_ui
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware tests/test_capture_buffer.c -o build/tests/test_capture_buffer
build/tests/test_capture_buffer
python3 tests/test_host_la.py
python3 tests/test_stream.py
python3 tests/test_demo.py
python3 tests/test_esp32_link_unit.py
# Compile the real UI against panel/button stubs: verify time rulers, square
# edges, pan/zoom bounds, independent pages, AUTO and redraw batching.
mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Wno-unused-function -Ifirmware -Iprotocol \
   tools/render_ui.c firmware/ui.c firmware/font6x8.c firmware/decoders.c \
   firmware/autodetect.c -o build/tests/render_ui
build/tests/render_ui --selftest
# The sigrok driver tests need the host stack built with scripts/build.sh host.
if [[ -x "$RP2350_ROOT/.local/bin/sigrok-cli" ]]; then
    python3 tests/test_integration.py
else
    echo 'sigrok-cli not built, skipping the driver integration tests' >&2
fi
