#!/usr/bin/env python3
"""End-to-end link check against the ESP32 signal generator on the bench.

Snapshot and streaming are two different data paths in the firmware, so a
decoder test on one says nothing about the other. This script exercises both
with the four protocols the generator actually emits, and checks the decoded
bytes rather than just "some edges arrived".

    python3 tests/test_esp32_link.py

Requires the board on /dev/ttyACM0, the ESP32 console on /dev/ttyUSB0 and the
host sigrok stack built by scripts/build.sh host.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import host_la  # noqa: E402
from host_la import open_port  # noqa: E402
from lar2 import (ANALYZE_RUNTIME, EDGE_FALLING, TRIGGER_EDGE,  # noqa: E402
                  analyze_payload)

CLI = os.path.join(ROOT, 'scripts', 'run-sigrok-cli.sh')
UART_PORT = os.environ.get('ESP32_PORT', '/dev/ttyUSB0')
LA_PORT = os.environ.get('LA_PORT', '/dev/ttyACM0')

# (label, trigger channel, sample rate, substring the decoder must print)
SNAPSHOT = [
    ('UART', 0, 25000000, 'LA1234'),
    ('I2C', 1, 25000000, '0x68 W ACK'),
    ('SPI', 3, 5000000, 'DE AD BE EF'),
    ('CAN', 7, 25000000, 'CAN ID=0x123'),
]

# sigrok decoder stacks. The .sr labels are CH0..CH7; see tools/stream_capture.
STACKS = {
    'UART': ('uart:rx=CH0:baudrate=115200', 'uart=rx-data'),
    'I2C': ('i2c:scl=CH1:sda=CH2', 'i2c=address-write'),
    'SPI': ('spi:clk=CH4:mosi=CH5:cs=CH3', 'spi=mosi-data'),
    'CAN': ('can:can_rx=CH7:nominal_bitrate=250000', 'can=id'),
}
STREAM_WANT = {'UART': '4C 41 31 32 33 34', 'I2C': 'Address write: 68',
               'SPI': 'DE AD BE EF', 'CAN': 'Identifier: 291'}


def show(label, ok, detail):
    print('%-22s %s  %s' % (label, 'PASS' if ok else 'FAIL', detail[:120]),
          flush=True)
    return bool(ok)


def device():
    fd = open_port(LA_PORT)
    d = host_la.Device.__new__(host_la.Device)
    d.fd, d.parser, d.tx_seq, d.rx_seq, d.pending = (
        fd, host_la.Parser(), 0, 0, [])
    d.hello()
    return d


def generator(cmd):
    import serial
    import time
    with serial.Serial(UART_PORT, 115200, timeout=1) as s:
        s.write((cmd + '\n').encode())
        time.sleep(0.4)
        return s.read(4096).decode('utf-8', 'replace')


def snapshot(d):
    print('\n== snapshot (triggered, decode_all on the device) ==', flush=True)
    ok = True
    for name, ch, rate, want in SNAPSHOT:
        try:
            # A late hardware trigger can leave less post-trigger history
            # than requested. Retry only that measurable window limitation;
            # never retry a bad decode from a complete window into a PASS.
            tail_us = {'UART': 800, 'I2C': 400, 'SPI': 200, 'CAN': 600}[name]
            for attempt in range(6):
                d.configure(rate, 65536, 0xFF, pre=32768,
                            trigger=(TRIGGER_EDGE, ch, EDGE_FALLING, 0, 0xFF))
                d.arm()
                samples, info = d.capture(timeout=20)
                index = info['trigger_index']
                if len(samples) != 65536 or not info['flags'] & 1 or not 0 <= index < len(samples):
                    raise RuntimeError('invalid triggered capture metadata')
                if len(samples) - index < rate * tail_us // 1000000:
                    print('snapshot %s retry %d/6: trigger=%d leaves too little post-trigger data'
                          % (name, attempt + 1, index), flush=True)
                    if attempt == 5:
                        raise RuntimeError('six captures without enough post-trigger history')
                    continue
                text = d.decode_all(115200, 250000).decode('utf-8', 'replace')
                ok &= show('snapshot ' + name, want in text,
                           'trig=%d %s' % (index, text.replace('\n', ' | ')))
                break
        except Exception as exc:
            ok &= show('snapshot ' + name, False, 'ERROR ' + str(exc))
            d.recover()
    return ok


def stream(d, seconds=6, out=None):
    """Run the real streaming CLI and return the .sr path."""
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'stream_capture.py'),
                    '-t', str(seconds), '-r', '1000000', '-o', out],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    # Streaming advances the device frame counter with its own SAMPLES frames;
    # restart the LAR2 session before using the host API again.
    d.tx_seq = 0
    d.hello()
    return out


def sigrok(path, name):
    pd, ann = STACKS[name]
    p = subprocess.run([CLI, '-i', path, '-P', pd, '-A', ann],
                       cwd=ROOT, text=True, capture_output=True, timeout=180)
    if p.returncode:
        return 'sigrok error: ' + p.stderr[:100]
    # sigrok prefixes every annotation with "spi-1: " / "uart-1: "; strip
    # that one prefix so the bytes form a readable contiguous string.
    lines = [re.sub(r'^\S+-1:\s*', '', ln) for ln in p.stdout.splitlines()]
    return ' '.join(ln.strip() for ln in lines if ln.strip())


def streaming(d, tmp):
    print('\n== streaming (1 MHz RLE, decode on the PC) ==', flush=True)
    ok = True
    path = os.path.join(tmp, 'all.sr')
    stream(d, 8, path)
    text = sigrok(path, 'UART') + sigrok(path, 'I2C') + sigrok(path, 'CAN')
    for name in ('UART', 'I2C', 'CAN'):
        ok &= show('stream ' + name, STREAM_WANT[name] in text,
                   'looking for %r' % STREAM_WANT[name])

    # SPI runs at 1 MHz by default, which is one sample per bit at the 1 MHz
    # streaming cap. Drop the generator clock for this one test, then restore
    # it so the bench is left as it was.
    generator('set spi 100')
    try:
        path = os.path.join(tmp, 'spi.sr')
        stream(d, 5, path)
        ok &= show('stream SPI', STREAM_WANT['SPI'] in sigrok(path, 'SPI'),
                   'generator SPI clock 100 kHz')
    finally:
        generator('set spi 1000')
    return ok


def main():
    if not os.path.exists(CLI):
        print('sigrok stack not built; run scripts/build.sh host first')
        return 2
    d = device()
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        ok &= snapshot(d)
        ok &= streaming(d, tmp)
    print('\n%s' % ('PASS: ESP32 -> RP2350 link OK in both modes'
                    if ok else 'FAIL: see the lines above'), flush=True)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
