# Review evidence before final QA

- `baseline`: original tool/device counters;PC silently missed424samples in30sRLE
  and968samples in15sraw. The old valid/drop labels are historical, not final acceptance.
- `stream-capacity`: repeated60s measurements before the final stale-DMA-IRQ fix;
  dense/raw1MHz explicitly INVALID,raw500kHz and compressible1MHzRLE lossless in those runs.
- `ui-controls`: screenshots read from the actual Pico framebuffer and navigation
  state/zoom assertions via the same queue as physical buttons. Panel photographs
  and physical switch contacts were not tested.
- `esp32-controls`: UART-off capture,square1kHz50%,bad numeric commands,hotSPIchanges,
  slow console typing/oversized lines and generator counters.
- `firmware-memory.txt`: ELF text/BSS statistics, not the sum of flash and RAM as a RAM claim.
- `ESP32-original-main.c`: backup before optimizing the external signal-generator source.

Use `../LATEST.txt` for final opt3 acceptance and the complete test matrix.
