#!/usr/bin/env python3
"""Host side test for streaming mode (tools/stream_capture.py).

Runs the real CLI against the PTY device model and checks that the decoded
samples match the emulated signal for both block encodings, and that the
resulting .sr really is a sigrok session PulseView can open.
"""
from pathlib import Path
import json
import io
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from emulator import Emulator  # noqa: E402
from lar2 import pattern, STREAM_BLOCK, RESULT
from stream_capture import StreamReceiver  # noqa: E402

CLI = str(ROOT / 'tools' / 'stream_capture.py')


def run_stream(device, *args):
    return subprocess.run([sys.executable, CLI, '--port', device.path, '-t', '1', *args],
                          capture_output=True, text=True, timeout=30)


class StreamTests(unittest.TestCase):
    def test_rle_roundtrip_to_sr(self):
        with Emulator('square') as device, tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'run.sr'
            proc = run_stream(device, '-o', str(out), '-r', '500000')
            self.assertEqual(proc.returncode, 0, proc.stderr)
            z = zipfile.ZipFile(out)
            names = sorted((n for n in z.namelist() if n.startswith('logic-1-')),
                           key=lambda n: int(n.rsplit('-', 1)[1]))
            got = b''.join(z.read(n) for n in names)
            self.assertEqual(got, pattern(65536, 500000, 'square'))
            meta = json.loads((Path(tmp) / 'run.json').read_text())
            self.assertEqual(meta['dropped'], 0)
            self.assertTrue(meta['rle'])
            self.assertIn('samplerate=500000', z.read('metadata').decode())

    def test_raw_roundtrip_to_raw_file(self):
        with Emulator('counter') as device, tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'run.raw'
            proc = run_stream(device, '-o', str(out), '--no-rle')
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(out.read_bytes(), pattern(65536, 1000000, 'counter'))

    def test_rate_duration_catches_consistent_but_invented_samples(self):
        receiver = StreamReceiver(io.BytesIO(), 0, rate=1000000)
        for seq in range(10):
            receiver.accept(STREAM_BLOCK, seq, b'\0' + (seq*500).to_bytes(4,'little') + bytes(500))
        prefix = b'\0' + (5000).to_bytes(4,'little') + bytes(4) + (5000).to_bytes(4,'little') + bytes(4)
        good = prefix + (5000).to_bytes(8,'little')
        self.assertEqual(receiver.counters(good)['device_sent'],5000)
        # Transport sequence and every count agree, but the declared interval
        # proves samples were duplicated/invented, as with a stale DMA IRQ.
        bad = prefix + (10000).to_bytes(8,'little')
        with self.assertRaisesRegex(ValueError,'rate/duration mismatch'):
            receiver.counters(bad)

    def test_rate_above_the_usb_limit_is_refused_locally(self):
        with Emulator('square') as device:
            proc = run_stream(device, '-o', '/dev/null', '-r', '5000000')
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn('USB CDC', proc.stderr)

    def test_tail_after_stop_is_not_discarded(self):
        with Emulator('counter', fault='stream-tail') as device, tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'tail.raw'
            proc = run_stream(device, '-o', str(out), '--no-rle')
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(out.read_bytes(), pattern(65536, 1000000, 'counter'))
            self.assertEqual(json.loads(out.with_suffix('.json').read_text())['device_sent'],65536)

    def test_counter_mismatch_fails(self):
        with Emulator('counter', fault='stream-count') as device, tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'wrong.raw'
            proc = run_stream(device, '-o', str(out), '--no-rle')
            self.assertNotEqual(proc.returncode,0)
            self.assertFalse(json.loads(out.with_suffix('.json').read_text())['valid'])

    def test_drops_are_invalid_and_channels_preserve_bits(self):
        with Emulator('counter', fault='stream-drop') as device, tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'loss.sr'
            proc = run_stream(device, '-o', str(out), '--no-rle', '--channels', '0,7')
            self.assertNotEqual(proc.returncode,0)
            m=json.loads(out.with_suffix('.json').read_text())
            self.assertEqual(m['dropped'],500)
            self.assertEqual(m['received_samples'],65036)
            self.assertFalse(m['valid'])
            with zipfile.ZipFile(out) as z:
                self.assertIn('probe8=CH7',z.read('metadata').decode())

    def test_sequences_offsets_and_rle_validation(self):
        receiver=StreamReceiver(io.BytesIO(),65535)
        receiver.samples=0xfffffffe
        receiver.accept(STREAM_BLOCK,65535,b'\0'+(0xfffffffe).to_bytes(4,'little')+b'AB')
        receiver.accept(STREAM_BLOCK,0,b'\0'+bytes(4)+b'CD')
        self.assertEqual(receiver.samples,0x100000002)
        self.assertEqual(receiver.sink.getvalue(),b'ABCD')
        with self.assertRaises(ValueError):receiver.accept(STREAM_BLOCK,4,b'\0'+bytes(4)+b'X')
        for body in (b'\x02'+bytes(4)+b'X', b'\1'+bytes(4)+b'\0X', b'\1'+bytes(4)+b'\1',
                     b'\1'+bytes(4)+b'\xffA\xffB'):
            with self.assertRaises(ValueError):StreamReceiver(io.BytesIO(),0).accept(STREAM_BLOCK,0,body)



if __name__ == '__main__':
    unittest.main()
