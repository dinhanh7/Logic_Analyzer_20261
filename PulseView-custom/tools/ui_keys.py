#!/usr/bin/env python3
"""Drive the on-device UI from USB while the four buttons are not wired.

Every key here goes through the same event queue as the physical buttons:

    u / d / o / b   short press of UP, DOWN, OK, BACK
    U / D / O / B   long press of the same button
    q               quit

Interactive: type keys straight into the terminal. One shot:
    python3 tools/ui_keys.py --port /dev/ttyACM0 --send ooD
"""
import argparse
import os
import select
import time
import sys
import termios
import tty

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_la
from host_la import open_port  # noqa: E402

HELP = """keys: u/d/o/b short press, U/D/O/B long press, q quits"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default=host_la.default_port())
    ap.add_argument('--send', help='send these keys and exit (e.g. ooD)')
    args = ap.parse_args()

    fd = open_port(args.port)
    try:
        if args.send:
            for key in args.send:
                if key not in 'udobUDOB':
                    raise ValueError('keys must be u/d/o/b or U/D/O/B')
                os.write(fd, key.encode())
                # UI captures emit SAMPLES as well: service USB until capture
                # finishes instead of leaving the device blocked in send_frame.
                until = time.monotonic() + (2.0 if key == 'O' else 0.05)
                while time.monotonic() < until:
                    if select.select([fd], [], [], min(0.02, max(0, until - time.monotonic())))[0]:
                        os.read(fd, 65536)
            return 0
        print(HELP)
        stdin = sys.stdin.fileno()
        saved = termios.tcgetattr(stdin)
        try:
            tty.setcbreak(stdin)
            while True:
                ready, _, _ = select.select([stdin, fd], [], [])
                if fd in ready:
                    os.read(fd, 65536)
                if stdin in ready:
                    key = sys.stdin.read(1)
                    if not key or key == 'q':
                        break
                    if key in 'udobUDOB':
                        os.write(fd, key.encode())
        finally:
            termios.tcsetattr(stdin, termios.TCSANOW, saved)
    finally:
        os.close(fd)
    return 0


if __name__ == '__main__':
    sys.exit(main())
