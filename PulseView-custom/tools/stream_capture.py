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
import shutil
import tempfile
import sys
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_la  # noqa: E402
from host_la import Device, open_port  # noqa: E402
from lar2 import (ANALYZE, ANALYZE_STREAM_START, ANALYZE_STREAM_STOP,
                  MAX_SAMPLES, RESULT, STREAM_BLOCK, ERROR, analyze_payload, frame)  # noqa: E402

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
        if hasattr(samples, 'read'):
            samples.seek(0)
            with z.open('logic-1-1', 'w', force_zip64=True) as target:
                shutil.copyfileobj(samples, target, 1024 * 1024)
        else:
            z.writestr('logic-1-1', samples)


class StreamReceiver:
    """Validate every block, including the STOP tail, into a bounded-memory sink."""
    def __init__(self, sink, sequence, rate=None):
        self.sink, self.sequence, self.rate = sink, sequence, rate
        self.samples = self.received = self.dropped = self.blocks = 0
        self.wire_bytes = self.raw_blocks = self.rle_blocks = 0

    def accept(self, kind, seq, payload):
        if seq != self.sequence:
            raise ValueError('stream sequence gap: %d != %d' % (seq, self.sequence))
        self.sequence = (seq + 1) & 0xffff
        self.wire_bytes += len(payload) + 16
        if kind == ERROR:
            raise RuntimeError(payload.decode('utf-8', 'replace'))
        if kind == RESULT:
            if len(payload) < 9 or payload[0] != 0:
                raise ValueError('invalid STREAM_STOP result')
            return payload
        if kind != STREAM_BLOCK or len(payload) < 6:
            raise ValueError('invalid stream block/header')
        enc, wire_offset, body = payload[0], int.from_bytes(payload[1:5], 'little'), payload[5:]
        if enc == 0:
            data = body
            self.raw_blocks += 1
        elif enc == 1:
            if len(body) % 2 or any(body[i] == 0 for i in range(0, len(body), 2)):
                raise ValueError('invalid RLE runs')
            # Firmware blocks represent at most 500 samples, preventing an
            # untrusted run count from allocating an arbitrary amount of RAM.
            if sum(body[::2]) > 500:
                raise ValueError('RLE block exceeds 500 samples')
            data = b''.join(bytes([body[i + 1]]) * body[i] for i in range(0, len(body), 2))
            self.rle_blocks += 1
        else:
            raise ValueError('unknown stream encoding %d' % enc)
        if not 0 < len(data) <= 500:
            raise ValueError('invalid expanded stream block size')
        # Offset is u32 on the wire; unwrap it around the next expected
        # sample, including long streams crossing 2**32 samples.
        delta = (wire_offset - (self.samples & 0xffffffff)) & 0xffffffff
        if delta >= 0x80000000:
            raise ValueError('duplicate or overlapping stream block')
        if delta:
            self.dropped += delta
            left = delta
            while left:
                n = min(left, 65536)
                self.sink.write(bytes(n))
                left -= n
        self.sink.write(data)
        self.samples += delta + len(data)
        self.received += len(data)
        self.blocks += 1
        return None

    def counters(self, result):
        sent = int.from_bytes(result[1:5], 'little')
        lost = int.from_bytes(result[5:9], 'little')
        if sent != self.received & 0xffffffff or lost != self.dropped & 0xffffffff:
            raise ValueError('host/device sample counters disagree: host %d/%d, device %d/%d'
                             % (self.received, self.dropped, sent, lost))
        meta = dict(device_sent=sent, device_dropped=lost)
        if len(result) >= 25:
            captured = int.from_bytes(result[9:13], 'little')
            faults = int.from_bytes(result[13:17], 'little')
            elapsed_us = int.from_bytes(result[17:25], 'little')
            if captured != self.samples & 0xffffffff:
                raise ValueError('device capture count differs from received sample span')
            if faults:
                raise ValueError('PIO/DMA hardware fault: 0x%x' % faults)
            if self.rate and elapsed_us:
                expected = self.rate * elapsed_us / 1e6
                # Allow DMA word/FIFO quantization on very short/slow runs.
                if abs(self.samples - expected) > max(32, expected * .001):
                    raise ValueError('sample rate/duration mismatch: %d samples in %.6fs at %dHz'
                                     % (self.samples, elapsed_us / 1e6, self.rate))
            meta.update(device_captured=captured, hardware_faults=faults,
                        capture_seconds=elapsed_us / 1e6)
        return meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default=host_la.default_port())
    ap.add_argument('-r', '--rate', type=int, default=1000000,
                    help='sample rate, max 1000000 (default)')
    ap.add_argument('-t', '--seconds', type=float, default=0,
                    help='stop after N seconds, 0 = until Ctrl+C')
    ap.add_argument('-o', '--out', default='stream.sr',
                    help='output file: .sr (PulseView) or .raw')
    ap.add_argument('--no-rle', action='store_true',
                    help='send raw sample bytes instead of RLE blocks')
    ap.add_argument('--allow-drops', action='store_true', help='save an explicitly invalid lossy capture (benchmark only)')
    ap.add_argument('--pause-seconds', type=float, default=0, help=argparse.SUPPRESS)
    ap.add_argument('--channels', default='0-7',
                    help='channel mask, default all eight')
    args = ap.parse_args()

    if args.seconds < 0:
        ap.error("seconds must be nonnegative")
    if args.rate not in (1000, 10000, 100000, 250000, 500000, 1000000):
        ap.error('USB CDC supported rates: 1000, 10000, 100000, 250000, 500000, 1000000 Hz')
    mask = 0
    for part in args.channels.split(','):
        if '-' in part:
            a, b = part.split('-')
            if not 0 <= int(a) <= int(b) <= 7:
                ap.error('channels must be within 0..7')
            for c in range(int(a), int(b) + 1):
                mask |= 1 << c
        else:
            if not 0 <= int(part) <= 7:
                ap.error('channels must be within 0..7')
            mask |= 1 << int(part)

    dev = Device(args.port)
    dev.hello()
    dev.configure(args.rate, MAX_SAMPLES, mask, 0, None)

    sink = tempfile.TemporaryFile()
    t0 = time.monotonic()  # Include START-reply latency in the requested wall-time budget.
    kind, seq, reply = dev.command(
        ANALYZE, analyze_payload(ANALYZE_STREAM_START, 0 if args.no_rle else 1, 0))
    if kind != RESULT or not reply or reply[0] != 0:
        dev.close()
        sink.close()
        sys.exit('device rejected STREAM_START (is the rate supported?)')
    receiver = StreamReceiver(sink, (seq + 1) & 0xffff, args.rate)
    print('streaming at %d Hz, %d channels, RLE %s; Ctrl+C to stop'
          % (args.rate, bin(mask).count('1'), 'off' if args.no_rle else 'on'),
          flush=True)

    start_reply_seconds = time.monotonic() - t0
    failure = None
    pause_done = False
    stop_result = None
    try:
        try:
            while not args.seconds or time.monotonic() - t0 < args.seconds:
                if args.pause_seconds and not pause_done and time.monotonic() - t0 > 1:
                    time.sleep(args.pause_seconds)  # Deliberate host stall for the fault benchmark.
                    pause_done = True
                remaining = args.seconds - (time.monotonic() - t0) if args.seconds else .2
                for kind, seq, payload in next_frames(dev, min(.2, max(0, remaining))):
                    if receiver.accept(kind, seq, payload) is not None:
                        raise ValueError('unexpected result before STREAM_STOP')
        except KeyboardInterrupt:
            print('stopping...', flush=True)
        # Read already parsed frames too; never discard blocks while STOP
        # flushes the final partial DMA slot.
        os.write(dev.fd, frame(ANALYZE, dev.tx_seq,
                              analyze_payload(ANALYZE_STREAM_STOP, 0, 0)))
        dev.tx_seq = (dev.tx_seq + 1) & 0xffff
        deadline = time.monotonic() + 10
        while stop_result is None and time.monotonic() < deadline:
            for kind, seq, payload in next_frames(dev):
                stop_result = receiver.accept(kind, seq, payload)
                if stop_result is not None:
                    break
        if stop_result is None:
            raise TimeoutError('no STREAM_STOP counters')
        counters = receiver.counters(stop_result)
        if not receiver.received:
            raise ValueError('no samples received')
    except Exception as exc:
        failure = str(exc)
        counters = {}
        # HELLO aborts a broken stream as well as a snapshot. Do not leave
        # the board busy after a parser/USB error.
        try:
            dev.tx_seq = 0
            dev.hello()
        except Exception:
            pass
    finally:
        dev.close()
    seconds = time.monotonic() - t0
    meta = dict(rate=args.rate, samples=receiver.samples, received_samples=receiver.received,
                mask=mask, channels=[i for i in range(8) if mask & (1 << i)],
                dropped=receiver.dropped, seconds=seconds, rle=not args.no_rle,
                blocks=receiver.blocks, raw_blocks=receiver.raw_blocks, rle_blocks=receiver.rle_blocks,
                wire_bytes=receiver.wire_bytes, host_mib_s=receiver.wire_bytes / seconds / 1048576,
                start_reply_seconds=start_reply_seconds,
                stop_counters_hex=stop_result.hex() if stop_result else None,
                valid=failure is None and receiver.dropped == 0, error=failure, **counters)
    if counters.get('capture_seconds'):
        meta['effective_sample_rate'] = receiver.samples / counters['capture_seconds']
    # Save bad runs for diagnosis but distinguish them in metadata and exit
    # status; a zero-filled gap is never advertised as a valid logic level.
    if receiver.received:
        sink.seek(0)
        if args.out.endswith('.raw'):
            with open(args.out, 'wb') as out:
                shutil.copyfileobj(sink, out, 1024 * 1024)
        else:
            # Each sample always contains the original eight bit positions.
            write_sr(args.out, sink, args.rate, 8)
    sink.close()
    with open(os.path.splitext(args.out)[0] + '.json', 'w') as out:
        json.dump(meta, out, indent=2)
    print('wrote %s: %d samples @ %d Hz, %d dropped, %.3f s; %s'
          % (args.out, receiver.samples, args.rate, receiver.dropped, seconds,
             'VERIFIED' if meta['valid'] else 'INVALID'), flush=True)
    if failure:
        print(failure, file=sys.stderr)
    return 0 if not failure and (not receiver.dropped or args.allow_drops) else 1


if __name__ == '__main__':
    sys.exit(main())
