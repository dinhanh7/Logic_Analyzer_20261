#!/usr/bin/env python3
"""Continuous capture ("streaming" mode) from the RP2350 logic analyzer.

Snapshot mode grabs one 262144-sample window and stops. This tool instead
runs the device in streaming mode: it captures continuously and ships 64 KB
slots to the PC as they fill, so the capture is limited by disk, not by SRAM.

    python3 tools/stream_capture.py -t 10 -o run.sr
    python3 tools/stream_capture.py -t 30 -r 500000 --no-rle -o slow.raw
    python3 tools/stream_capture.py            # stop with Ctrl+C

Rate is capped at 1 MHz by USB CDC full speed (1 byte per sample = 1 MB/s).
RLE is on by default: idle lines and square waves compress well, a fast
toggling line falls back to raw automatically per block.

Output:
  *.sr   sigrok session (opens in PulseView), unless the name ends in .raw
  *.raw  one byte per sample, bit n = channel n (if -o ends in .raw)
  *.json metadata next to either file
"""
import argparse
import json
import os
import select
import sys
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_la  # noqa: E402
from host_la import Device, open_port  # noqa: E402
from lar2 import (ANALYZE, ANALYZE_STREAM_START, ANALYZE_STREAM_STOP,
                  MAX_SAMPLES, RESULT, STREAM_BLOCK, analyze_payload, frame)  # noqa: E402

MAX_STREAM_RATE = 1000000


def next_frames(dev, timeout=0.2):
    """Frames already parsed by Device.command() come first; blocking reads of
    the port would skip them and leave holes in the sample stream."""
    if dev.pending:
        while dev.pending:
            yield dev.pending.pop(0)
        return
    ready, _, _ = select.select([dev.fd], [], [], timeout)
    if ready:
        yield from dev.parser.feed(os.read(dev.fd, 65536))


def write_sr(path, samples, rate, channels=8):
    """Minimal sigrok session: a zip with metadata and the sample stream."""
    probes = ''.join('probe%d=CH%d\n' % (i + 1, i) for i in range(channels))
    meta = ('[device 1]\n'
            'capturefile=logic-1\n'
            'total probes=%d\n'
            'samplerate=%d\n'
            'unitsize=1\n'
            '%s' % (channels, rate, probes))
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('version', '2')
        z.writestr('metadata', meta)
        z.writestr('logic-1-1', samples)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('-r', '--rate', type=int, default=1000000,
                    help='sample rate, max 1000000 (default)')
    ap.add_argument('-t', '--seconds', type=float, default=0,
                    help='stop after N seconds, 0 = until Ctrl+C')
    ap.add_argument('-o', '--out', default='stream.sr',
                    help='output file: .sr (PulseView) or .raw')
    ap.add_argument('--no-rle', action='store_true',
                    help='send raw sample bytes instead of RLE blocks')
    ap.add_argument('--channels', default='0-7',
                    help='channel mask, default all eight')
    args = ap.parse_args()

    if args.rate > MAX_STREAM_RATE:
        ap.error('rate above %d is not reachable over USB CDC' % MAX_STREAM_RATE)
    mask = 0
    for part in args.channels.split(','):
        if '-' in part:
            a, b = part.split('-')
            for c in range(int(a), int(b) + 1):
                mask |= 1 << c
        else:
            mask |= 1 << int(part)

    dev = Device(args.port)
    dev.hello()
    dev.configure(args.rate, MAX_SAMPLES, mask, 0, None)

    out = bytearray()
    expect = 0          # next absolute sample offset
    dropped = 0
    blocks = 0

    kind, _, reply = dev.command(
        ANALYZE, analyze_payload(ANALYZE_STREAM_START, 0 if args.no_rle else 1, 0))
    if kind != RESULT or not reply or reply[0] != 0:
        sys.exit('device rejected STREAM_START (is the rate supported?)')
    print('streaming at %d Hz, %d channels, RLE %s; Ctrl+C to stop'
          % (args.rate, bin(mask).count('1'), 'off' if args.no_rle else 'on'),
          flush=True)

    t0 = time.time()
    try:
        while True:
            if args.seconds and time.time() - t0 >= args.seconds:
                break
            for kind, seq, payload in next_frames(dev):
                if kind != STREAM_BLOCK or len(payload) < 5:
                    continue
                enc = payload[0]
                offset = int.from_bytes(payload[1:5], 'little')
                body = payload[5:]
                if enc == 0:
                    data = body
                else:
                    data = bytearray()
                    for i in range(0, len(body) - 1, 2):
                        data.extend(bytes([body[i + 1]]) * body[i])
                blocks += 1
                if offset < expect:
                    continue                      # already have it
                if offset > expect:               # device dropped slots
                    dropped += offset - expect
                    out.extend(bytes(offset - expect))
                out.extend(data)
                expect = offset + len(data)
                if blocks % 200 == 0:
                    print('  %.1f s  %d samples  %d dropped'
                          % (time.time() - t0, len(out), dropped), flush=True)
    except KeyboardInterrupt:
        print('\nstopped by hand', flush=True)

    # Stop and read the final counters. The device may still be flushing
    # blocks, so drain frames until the RESULT arrives.
    os.write(dev.fd, frame(ANALYZE, dev.tx_seq,
                           analyze_payload(ANALYZE_STREAM_STOP, 0, 0)))
    dev.tx_seq = (dev.tx_seq + 1) & 0xffff
    deadline = time.time() + 5.0
    stop_result = None
    while time.time() < deadline:
        ready, _, _ = select.select([dev.fd], [], [], 0.2)
        if not ready:
            continue
        for kind, seq, payload in next_frames(dev):
            if kind == STREAM_BLOCK and len(payload) >= 5:
                continue
            if kind == RESULT and len(payload) >= 9:
                stop_result = payload
                break
        if stop_result:
            break
    dev.close()

    if not out:
        print('no samples received', file=sys.stderr)
        return 1

    if args.out.endswith('.raw'):
        with open(args.out, 'wb') as f:
            f.write(out)
    else:
        write_sr(args.out, bytes(out), args.rate, bin(mask).count('1'))
    meta = dict(rate=args.rate, samples=len(out), mask=mask,
                dropped=dropped, seconds=round(time.time() - t0, 3),
                rle=not args.no_rle)
    with open(os.path.splitext(args.out)[0] + '.json', 'w') as f:
        json.dump(meta, f, indent=2)
    print('\nwrote %s: %d samples @ %d Hz, %d dropped, %.2f s'
          % (args.out, len(out), args.rate, dropped, time.time() - t0))
    if dropped:
        print('dropped samples: USB CDC tops out near 1 MB/s. Keep RLE on '
              'or lower the rate (raw is comfortable up to 500 kHz).')
    if stop_result:
        sent = int.from_bytes(stop_result[1:5], 'little')
        lost = int.from_bytes(stop_result[5:9], 'little')
        print('device reports %d samples sent, %d dropped' % (sent, lost))
    return 0


if __name__ == '__main__':
    sys.exit(main())
