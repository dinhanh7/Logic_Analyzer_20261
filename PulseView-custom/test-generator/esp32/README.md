# ESP32 reference signal generator

This is the maintained source used on the bench, copied from the original local
`la-signal-gen` project so the Pico tests can be reproduced. Requires ESP-IDF6.0.1.
All output levels are3.3V; use common GND. See `../../docs/DEMO.md` for wiring.

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

Console115200: `help`, `status`, `freq <hz>`, `uart on|off`, `set uart <baud>`,
`set i2c <khz>`, `set spi <khz>`, `set can <kbps>`, `burst on|off`.
`stress <hz>|off` temporarily uses GPIO26/CH0 for a50% square wave instead of UART;
this is a throughput/timing diagnostic, not an additional simultaneous UART source.

LEDC channels are distinct. GPIO25 is reserved for SPI MISO; SQ2 is disabled.
Loop back GPIO23→25 if MISO must echo MOSI. Keep one console open and wait for boot;
opening CP2102 can toggle reset lines. `tools/demo.py` and the QA runner handle this.

Snapshot uses SPI1MHz; streaming1MHz must lower SPI to100kHz then restore1MHz.
CAN is standard11-bit ID0x123,DLC4,NO-ACK self-test on GPIO32; this does not test
an external differential CAN transceiver or arbitration between real bus nodes.
