#!/usr/bin/env python3
"""Host side test for streaming mode (tools/stream_capture.py).

Runs the real CLI against the PTY device model and checks that the decoded
samples match the emulated signal for both block encodings, and that the
resulting .sr really is a sigrok session PulseView can open.
"""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from emulator import Emulator  # noqa: E402
from lar2 import pattern  # noqa: E402

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

    def test_rate_above_the_usb_limit_is_refused_locally(self):
        with Emulator('square') as device:
            proc = run_stream(device, '-o', '/dev/null', '-r', '5000000')
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn('USB CDC', proc.stderr)


if __name__ == '__main__':
    unittest.main()
