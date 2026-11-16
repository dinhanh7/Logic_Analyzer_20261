#!/usr/bin/env python3
"""Walk the four TFT screens every few seconds, without physical buttons.

    python3 tools/ui_cycle.py              # 3 s per screen, forever (mock data)
    python3 tools/ui_cycle.py -i 5         # 5 s per screen
    python3 tools/ui_cycle.py --rounds 2   # two laps, then stop
    python3 tools/ui_cycle.py --capture    # real capture instead of the mock

Screen flow (spec):
    1 CONFIG --OK on [BAT DAU THU]--> 3 WAVEFORM --OK--> 4 DECODE
    4 DECODE --BACK--> 3 WAVEFORM --BACK--> 1 CONFIG --BACK--> 2 TRIGGER
    2 TRIGGER --BACK--> 1 CONFIG

By default the firmware's built-in mock waveform is loaded
(LAR2_ANALYZE_UI_DEMO), so screen 3 has real edges and screen 4 decodes a real
UART "HELLO" frame even with no signal generator attached. Pass --capture to
arm a real capture instead.
"""
import argparse
import os
import select
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_la  # noqa: E402
from host_la import open_port  # noqa: E402
from lar2 import ANALYZE, ANALYZE_RUNTIME, ANALYZE_UI_DEMO, analyze_payload  # noqa: E402

SCREEN_NAME = ['01 CONFIG', '02 TRIGGER', '03 WAVEFORM', '04 DECODE']

SCREEN_HINT = [
    'header 1/4, 4 dong gia tri + [ BAT DAU THU ] co khung',
    'header 2/4, Che do/kenh/Dieu kien, Baud dim khi khong phai UART',
    'header 3/4 [25M], 4 tracks + trigger do + cursor giua, T:/STOP',
    'header 4/4, < UART >, RX:CH0 115200, phai thay dong decode HELLO',
]

# (key, expected screen, note) - the check the firmware state must pass
STEPS = [
    ('o', 3, 'OK   -> decode'),
    ('b', 2, 'BACK -> waveform'),
    ('b', 0, 'BACK -> config'),
    ('b', 1, 'BACK -> trigger'),
    ('b', 0, 'BACK -> config'),
]


def screen_id(dev):
    kind, seq, payload = dev.command(ANALYZE, analyze_payload(ANALYZE_RUNTIME, 0, 0))
    return payload[30] if len(payload) == 32 else None


def drain(fd, seconds=0.4):
    """Throw away frames a previous host session left queued."""
    end = time.time() + seconds
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                os.read(fd, 4096)
            except OSError:
                return


def to_config(dev, fd):
    """BACK walks CONFIG <-> TRIGGER, so poll instead of assuming a parity."""
    for _ in range(6):
        if screen_id(dev) == 0:
            return True
        os.write(fd, b'b')
        time.sleep(0.4)
    return False


def show(dev, want):
    got = None
    try:
        got = screen_id(dev)
    except Exception:
        pass
    mark = '' if got is None else ('  [device %s]' % (
        'OK' if got == want else 'SAI: man %d' % got))
    print('%-12s %s%s' % (SCREEN_NAME[want], SCREEN_HINT[want], mark), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('-i', '--interval', type=float, default=3.0,
                    help='seconds per screen (default 3)')
    ap.add_argument('--rounds', type=int, default=0,
                    help='number of laps, 0 = forever (default)')
    ap.add_argument('--capture', action='store_true',
                    help='arm a real capture instead of loading the mock waveform')
    args = ap.parse_args()

    fd = open_port(args.port)
    dev = host_la.Device.__new__(host_la.Device)
    dev.fd, dev.parser, dev.tx_seq, dev.rx_seq, dev.pending = (
        fd, host_la.Parser(), 0, 0, [])
    try:
        drain(fd)
        dev.hello()
        lap = 0
        while args.rounds == 0 or lap < args.rounds:
            if args.capture:
                # From CONFIG: four DOWNs reach [ BAT DAU THU ], OK captures
                # and the UI jumps to WAVEFORM by itself.
                if not to_config(dev, fd):
                    print('could not reach screen 01, aborting', flush=True)
                    return 1
                os.write(fd, b'dddd')
                time.sleep(0.4)
                os.write(fd, b'o')
            else:
                dev.command(ANALYZE, analyze_payload(ANALYZE_UI_DEMO, 0, 0))
            time.sleep(1.0)
            show(dev, 2)
            time.sleep(args.interval)
            for key, want, note in STEPS:
                print('             %s' % note, flush=True)
                time.sleep(args.interval)
                os.write(fd, key.encode())
                time.sleep(0.4)
                show(dev, want)
            lap += 1
        print('done; the board is on screen %s' % SCREEN_NAME[screen_id(dev) or 0])
    except KeyboardInterrupt:
        print('\nstopped by hand', flush=True)
    finally:
        os.close(fd)


if __name__ == '__main__':
    main()
