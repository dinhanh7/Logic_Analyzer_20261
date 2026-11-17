# Validation of the real-capture demo support

**Update:** hardware access is now available and the full suite, ESP32 check and real TFT/PulseView live demo have passed. See [real-run](real-run/README.md) for current results. The blocked logs below are historical.

- `build.log`: Pico 2 firmware and patched PulseView build completed.
- `demo-tests.log`: 8 Python tests passed, including native Qt/GVariant preset parsing for both recorded and live sessions.
- `suite-default.log`: normal suite stopped at LeakSanitizer's ptrace limitation.
- `suite-no-lsan.log`: C codec/trigger/decode/autodetect/UI, host 7/7, stream 3/3, demo 8/8 and render selftest passed with `ASAN_OPTIONS=detect_leaks=0`. Integration did not pass: 9 methods produced 19 failures including subtests, with libusb initialization errors before acquisition.
- `esp32-attempt.log`: hardware test could not open `/dev/ttyACM0`.
- `flash-attempt.log`: neither running board nor BOOTSEL mount was visible; no flash was performed.
- `preflight.log`: demo correctly rejected missing hardware instead of showing a synthetic success.

User authorization for full access was given. Retrying USB/display/network still failed, Orca runtime returned `runtime_access_denied` (EPERM), and an explicit out-of-sandbox request was automatically rejected by the session's `sandbox_approval: false` policy. Therefore these files do not prove a successful hardware/GUI demo. Run `bash scripts/demo.sh` from the connected desktop to obtain real capture, TFT readback, PulseView annotations and a screenshot. See [DEMO.md](../DEMO.md).

Logs are preserved as emitted, including trailing whitespace and negative results.
