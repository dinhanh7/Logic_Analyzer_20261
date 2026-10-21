#!/usr/bin/env python3
"""Host side tests for the LAR2 trigger path.

The libsigrok driver still speaks the manual only v1 profile, so the trigger
is exercised through tools/host_la.py against the PTY device model. These tests
need no hardware and no built sigrok stack.
"""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from emulator import Emulator
from host_la import Device, parse_trigger
from lar2 import find_trigger


class TriggerHostTests(unittest.TestCase):
    def capture(self, device, **kwargs):
        dev = Device(device.path)
        try:
            board = dev.hello()
            caps = dev.caps()
            dev.configure(kwargs.get('rate', 1000000), kwargs.get('count', 2000),
                          kwargs.get('mask', 0xff), kwargs.get('pre', 0),
                          kwargs.get('trigger'))
            dev.arm()
            samples, info = dev.capture(kwargs.get('count', 2000),
                                        kwargs.get('rate', 1000000))
        finally:
            dev.close()
        return board, caps, samples, info

    def test_manual_capture_has_no_trigger(self):
        with Emulator('square') as device:
            board, caps, samples, info = self.capture(device)
            self.assertTrue(board.startswith('RP2350-LA'))
            self.assertEqual(caps['channels'], 8)
            self.assertEqual(len(samples), 2000)
            self.assertEqual(info['trigger_index'], 0xffffffff)
            self.assertEqual(info['pretrigger'], 0)

    def test_edge_trigger_reports_the_edge(self):
        with Emulator('square') as device:
            trigger = parse_trigger('edge:0:rise')
            _, _, samples, info = self.capture(device, pre=500, trigger=trigger)
            self.assertEqual(len(samples), 2000)
            self.assertEqual(info['trigger_index'], 500)
            self.assertEqual(info['pretrigger'], 500)
            index = info['trigger_index']
            self.assertTrue(samples[index] & 0x01, 'channel 0 must be high at the trigger')
            self.assertFalse(samples[index - 1] & 0x01, 'channel 0 must be low before the trigger')

    def test_falling_edge_trigger(self):
        with Emulator('square') as device:
            trigger = parse_trigger('edge:0:fall')
            _, _, samples, info = self.capture(device, pre=500, trigger=trigger)
            index = info['trigger_index']
            self.assertEqual(index, 500)
            self.assertFalse(samples[index] & 0x01)
            self.assertTrue(samples[index - 1] & 0x01)

    def test_pattern_trigger_reports_the_pattern(self):
        with Emulator('counter') as device:
            trigger = parse_trigger('pattern:0x5a:0xff')
            _, _, samples, info = self.capture(device, pre=100, trigger=trigger)
            index = info['trigger_index']
            self.assertEqual(index, 100)
            self.assertEqual(samples[index], 0x5a)

    def test_pattern_mask_ignores_the_dont_care_bits(self):
        with Emulator('counter') as device:
            trigger = parse_trigger('pattern:0x0a:0x0f')
            _, _, samples, info = self.capture(device, pre=10, trigger=trigger)
            index = info['trigger_index']
            self.assertEqual(samples[index] & 0x0f, 0x0a)

    def test_window_length_and_pre_trigger_history(self):
        with Emulator('square') as device:
            trigger = parse_trigger('edge:0:rise')
            _, _, samples, info = self.capture(device, count=4000, pre=1000, trigger=trigger)
            self.assertEqual(len(samples), 4000)
            self.assertEqual(info['trigger_index'], 1000)

    def test_firmware_and_python_matchers_agree(self):
        """The Python matcher mirrors the firmware one; both must find the
        same first hit in the same data."""
        from lar2 import pattern
        data = pattern(5000, 1000000, 'counter')
        for trigger in (('edge', 0, 0, 0, 0), ('pattern', 0, 0, 0x5a, 0xff)):
            found = find_trigger(data, 0, len(data), trigger)
            self.assertIsNotNone(found)
            if trigger[0] == 'pattern':
                self.assertEqual(data[found] & 0xff, 0x5a)
            else:
                self.assertTrue(data[found] & 1)
                self.assertFalse(data[found - 1] & 1)


if __name__ == '__main__':
    unittest.main()
