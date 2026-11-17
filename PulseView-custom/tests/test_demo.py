#!/usr/bin/env python3
"""Demo must fail on missing hardware, missing/hidden/wrong GUI annotations."""
from copy import deepcopy
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import demo


def valid_report():
    report = dict(channels=[f'CH{i}' for i in range(8)], decoders=[
        dict(name=n, annotations=[dict(text=v[2], **{'class': demo.GUI_CLASS[n]},
                                       visible=True, sample=1)])
        for n, v in demo.PROTOCOLS.items()])
    report['decoders'][1]['annotations'] += [
        dict(text='Data write: 01', **{'class': 'data-write'}, visible=True, sample=2),
        dict(text='Data write: 5A', **{'class': 'data-write'}, visible=True, sample=3)]
    return report


class DemoChecks(unittest.TestCase):
    def test_visible_correct_gui_annotations(self):
        demo.check_gui_report(valid_report())

    def test_generic_bits_do_not_prove_decode(self):
        r = valid_report()
        r['decoders'][0]['annotations'][0]['class'] = 'rx-data-bit'
        with self.assertRaises(RuntimeError): demo.check_gui_report(r)

    def test_hidden_annotations_do_not_count(self):
        r = valid_report()
        r['decoders'][2]['annotations'][0]['visible'] = False
        with self.assertRaises(RuntimeError): demo.check_gui_report(r)

    def test_wrong_byte_or_missing_channel_fails(self):
        for mutate in (lambda r: r['channels'].pop(),
                       lambda r: r['decoders'][2]['annotations'][0].update(text='DE AD BE 00')):
            r = deepcopy(valid_report()); mutate(r)
            with self.assertRaises(RuntimeError): demo.check_gui_report(r)

    def test_annotation_sample_order_is_used(self):
        r = valid_report(); base = dict(r['decoders'][0]['annotations'][0])
        r['decoders'][0]['annotations'] = [dict(base, text=t, sample=i)
                                           for i, t in reversed(list(enumerate('4C 41 31 32 33 34'.split())))]
        demo.check_gui_report(r)

    def test_tft_requires_real_bytes_and_ack(self):
        table = '01|0xD0|ADR|0\n02|--|ACK|1us\n03|0x01|.|1us\n04|--|ACK|1us\n05|0x5A|Z|1us'
        demo.check_tft_table('I2C', table)
        for broken in (table.replace('0x5A', '0x00'), table.replace('ACK', 'NAK'), '(no packets)'):
            with self.assertRaises(RuntimeError): demo.check_tft_table('I2C', broken)

    def test_missing_hardware_exits_nonzero_and_records_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'result'
            p = subprocess.run([sys.executable, str(ROOT / 'tools/demo.py'), '--no-gui',
                '--port', str(Path(tmp) / 'missing-pico'), '--output', str(out)],
                capture_output=True, text=True)
            self.assertEqual(p.returncode, 1)
            summary = json.loads((out / 'summary.json').read_text())
            self.assertEqual(summary['status'], 'FAIL')
            self.assertEqual(summary['gui'], 'NOT_RUN')
            self.assertFalse((out / 'capture.sr').exists())

    @unittest.skipUnless(shutil.which('pkg-config') and shutil.which('c++'), 'native Qt toolchain missing')
    def test_qsettings_reads_both_recorded_and_live_presets(self):
        probe = subprocess.run(['pkg-config', '--cflags', '--libs', 'Qt5Core', 'glib-2.0'],
                               capture_output=True, text=True)
        if probe.returncode: self.skipTest('Qt5Core development files missing')
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'check'
            subprocess.run(['c++', '-std=c++11', '-fPIC', str(ROOT / 'tests/check_demo_setup.cpp'),
                            '-o', str(binary), *shlex.split(probe.stdout)], check=True)
            for live in (False, True):
                path = Path(tmp) / 'capture.pvs'
                demo.write_setup(path, live=live)
                subprocess.run([str(binary), str(path)], check=True)


if __name__ == '__main__': unittest.main(verbosity=2)
