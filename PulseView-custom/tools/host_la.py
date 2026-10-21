#!/usr/bin/env python3
"""Talk to a RP2350 logic analyzer over USB CDC using the LAR2 protocol.

This is the host side of the trigger work: the libsigrok driver still speaks
the manual-only v1 profile, so the trigger is driven from here first. The
capture is written as a raw sample file (one byte per sample, bit n = channel
n), which is the same layout PulseView receives over SUMP.
"""
import argparse
import os
import select
import struct
import sys
import termios
import time
import zlib

from lar2 import (ACK, ARM, CAPS, CONFIG, END, ERROR, GET_CAPS, HELLO, ID, INFO,
                  MAX_PAYLOAD, MAX_SAMPLES, RATES, SAMPLES, Parser,
                  config_payload, frame)

TRIGGER_MANUAL, TRIGGER_EDGE, TRIGGER_PATTERN = 0, 1, 2
EDGE_RISING, EDGE_FALLING = 0, 1
NO_TRIGGER = 0xffffffff


def open_port(path):
    import fcntl
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP
                  | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON
                  | termios.IXOFF | termios.IXANY)
    attrs[1] &= ~termios.OPOST
    attrs[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
    attrs[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG
                  | termios.IEXTEN)
    attrs[4] = attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


class Device:
    def __init__(self, path):
        self.fd = open_port(path)
        self.parser = Parser()
        self.tx_seq = 0
        self.rx_seq = 0
        self.pending = []

    def close(self):
        os.close(self.fd)

    def _read(self, deadline):
        while True:
            if self.pending:
                return self.pending.pop(0)
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError('no frame from device')
            ready, _, _ = select.select([self.fd], [], [], min(remaining, 0.2))
            if not ready:
                continue
            chunk = os.read(self.fd, 4096)
            if chunk:
                self.pending.extend(self.parser.feed(chunk))

    def command(self, kind, payload=b'', timeout=2.0):
        os.write(self.fd, frame(kind, self.tx_seq, payload))
        self.tx_seq = (self.tx_seq + 1) & 0xffff
        return self._read(time.time() + timeout)

    def hello(self):
        kind, seq, payload = self.command(HELLO)
        if kind != ID:
            raise RuntimeError('expected ID, got 0x%02x' % kind)
        return payload.decode('utf-8', 'replace')

    def caps(self):
        kind, seq, payload = self.command(GET_CAPS)
        if kind != CAPS:
            raise RuntimeError('expected CAPS, got 0x%02x' % kind)
        channels, unit, flags, max_samples, count = struct.unpack_from('<BBHII', payload)
        rates = struct.unpack_from('<%dQ' % count, payload, 12)
        return dict(channels=channels, unit=unit, flags=flags,
                    max_samples=max_samples, rates=list(rates))

    def configure(self, rate, count, mask, pre=0, trigger=None):
        payload = config_payload(rate, count, mask, pre, trigger)
        kind, seq, payload = self.command(CONFIG, payload)
        if kind == ERROR:
            raise RuntimeError('CONFIG rejected: %s' % payload.decode('utf-8', 'replace'))
        if kind != ACK:
            raise RuntimeError('expected ACK, got 0x%02x' % kind)

    def arm(self):
        kind, seq, payload = self.command(ARM)
        if kind == ERROR:
            raise RuntimeError('ARM rejected: %s' % payload.decode('utf-8', 'replace'))
        if kind != ACK:
            raise RuntimeError('expected ACK, got 0x%02x' % kind)

    def capture(self, count, rate, timeout=60.0):
        """Read one capture and return (samples, info dict)."""
        deadline = time.time() + timeout
        kind, seq, payload = self._read(deadline)
        if kind == ERROR:
            raise RuntimeError('device error: %s' % payload.decode('utf-8', 'replace'))
        if kind != INFO:
            raise RuntimeError('expected INFO, got 0x%02x' % kind)
        info = dict(zip(('rate', 'count', 'trigger_index', 'pretrigger',
                         'channels', 'unit', 'mask', 'flags'),
                        struct.unpack('<QIII4B', payload)))
        out = bytearray()
        crc = 0
        while True:
            kind, seq, payload = self._read(deadline)
            if kind == ERROR:
                raise RuntimeError('device error: %s' % payload.decode('utf-8', 'replace'))
            if kind == SAMPLES:
                out += payload
                crc = zlib.crc32(payload, crc)
            elif kind == END:
                total, device_crc = struct.unpack('<II', payload)
                if total != len(out):
                    raise RuntimeError('sample count mismatch: %d != %d' % (len(out), total))
                if device_crc != zlib.crc32(out):
                    raise RuntimeError('capture CRC mismatch')
                return bytes(out), info
            else:
                raise RuntimeError('unexpected frame 0x%02x during capture' % kind)


def parse_rate(text):
    text = text.lower().strip()
    units = {'k': 1000, 'm': 1000000}
    if text[-1] in units:
        return int(float(text[:-1]) * units[text[-1]])
    return int(text)


def parse_trigger(text):
    if text is None:
        return None
    parts = text.split(':')
    kind = parts[0].lower()
    if kind == 'edge':
        channel = int(parts[1])
        edge = EDGE_FALLING if parts[2].lower() in ('fall', 'falling', 'down') else EDGE_RISING
        return (TRIGGER_EDGE, channel, edge, 0, 0)
    if kind == 'pattern':
        value = int(parts[1], 0)
        mask = int(parts[2], 0) if len(parts) > 2 else 0xff
        return (TRIGGER_PATTERN, 0, 0, value, mask)
    raise ValueError('unknown trigger %r' % text)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True, help='serial device, e.g. /dev/ttyACM0')
    ap.add_argument('--rate', default='1m', help='sample rate, e.g. 1k, 100k, 1m, 5m, 25m')
    ap.add_argument('--count', type=int, default=100000, help='samples in the window')
    ap.add_argument('--pre', type=int, default=0, help='samples kept before the trigger')
    ap.add_argument('--mask', default='0xff', help='channel mask, e.g. 0x0f')
    ap.add_argument('--trigger', help='edge:CH:rise|fall or pattern:VALUE[:MASK]')
    ap.add_argument('--out', help='write the raw samples to this file')
    ap.add_argument('--timeout', type=float, default=60.0)
    args = ap.parse_args()

    rate = parse_rate(args.rate)
    trigger = parse_trigger(args.trigger)
    dev = Device(args.port)
    try:
        board = dev.hello()
        caps = dev.caps()
        print('board: %s' % board)
        print('caps: %d channels, max %d samples, rates %s'
              % (caps['channels'], caps['max_samples'],
                 ', '.join('%g' % r for r in caps['rates'])))
        if rate not in caps['rates']:
            raise SystemExit('rate %d not supported by the device' % rate)
        if not 1 <= args.count <= min(caps['max_samples'], MAX_SAMPLES):
            raise SystemExit('count out of range')
        pre = args.pre
        if trigger is not None and not 0 <= pre < args.count:
            raise SystemExit('pre-trigger must be smaller than count')
        dev.configure(rate, args.count, int(args.mask, 0), pre, trigger)
        dev.arm()
        samples, info = dev.capture(args.count, rate, args.timeout)
    finally:
        dev.close()

    print('captured %d samples at %g Hz' % (len(samples), info['rate']))
    print('trigger index: %s' % ('none' if info['trigger_index'] == NO_TRIGGER
                                 else info['trigger_index']))
    print('pre-trigger samples: %d, mask 0x%02x, flags 0x%02x'
          % (info['pretrigger'], info['mask'], info['flags']))
    if args.out:
        with open(args.out, 'wb') as handle:
            handle.write(samples)
        print('wrote %s' % args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
