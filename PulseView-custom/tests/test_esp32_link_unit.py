"""The hardware test may retry short windows, never incorrect decoded data."""
import contextlib
import io
import unittest
import test_esp32_link as link


class Capture:
    def __init__(self, late=False, text=b'LA1234 0x68 W ACK DE AD BE EF CAN ID=0x123'):
        self.late, self.text, self.calls, self.decodes = late, text, 0, 0

    def configure(self, *args, **kwargs): pass
    def arm(self): pass
    def recover(self): raise AssertionError('unexpected protocol exception')

    def capture(self, timeout):
        self.calls += 1
        index = 65000 if self.late and self.calls == 1 else 32768
        return bytes(65536), dict(trigger_index=index, flags=1)

    def decode_all(self, *args):
        self.decodes += 1
        return self.text


class WindowChecks(unittest.TestCase):
    def test_recaptures_measurably_short_window(self):
        dev = Capture(late=True)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(link.snapshot(dev))
        self.assertEqual(dev.calls, 5)
        self.assertEqual(dev.decodes, 4)

    def test_wrong_bytes_in_complete_window_are_failures(self):
        dev = Capture(text=b'wrong decoded bytes')
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(link.snapshot(dev))
        self.assertEqual(dev.calls, 4)
        self.assertEqual(dev.decodes, 4)


if __name__ == '__main__': unittest.main()
