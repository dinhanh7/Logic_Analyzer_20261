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
import sys
import termios
import tty

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_la import open_port  # noqa: E402

HELP = """keys: u/d/o/b short press, U/D/O/B long press, q quits"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--send', help='send these keys and exit (e.g. ooD)')
    args = ap.parse_args()

    fd = open_port(args.port)
    try:
        if args.send:
            os.write(fd, args.send.encode())
            return 0
        print(HELP)
        stdin = sys.stdin.fileno()
        saved = termios.tcgetattr(stdin)
        try:
            tty.setcbreak(stdin)
            while True:
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
