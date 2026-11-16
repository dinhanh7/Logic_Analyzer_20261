#!/usr/bin/env python3
"""Walk the five TFT screens every few seconds, without physical buttons.

    python3 tools/ui_cycle.py              # 3 s per screen, one lap (mock data)
    python3 tools/ui_cycle.py -i 5 --rounds 3
    python3 tools/ui_cycle.py --protocols  # decode screen: UART/I2C/SPI/CAN
    python3 tools/ui_cycle.py --capture    # real capture instead of the mock

Screen flow (spec):
    1 CONFIG --OK on BAT DAU--> 3 WAVE CH0-3 --OK--> 4 WAVE CH4-7 --OK--> 5 DECODE
    5 DECODE --BACK--> 4 --BACK--> 3 --BACK--> 1 CONFIG --BACK--> 2 TRIGGER

Every step is checked against the firmware's own state (LAR2_ANALYZE_RUNTIME
byte 30), so a wrong printout here is a logic bug, not a display bug.
--protocols additionally cycles the decode screen through all four protocols
and dumps the decoder text with LAR2_ANALYZE_UI_TEXT.
"""
import argparse
import os
import select
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_la  # noqa: E402
from host_la import open_port  # noqa: E402
from lar2 import (ANALYZE, ANALYZE_RUNTIME, ANALYZE_UI_DEMO,  # noqa: E402
                  ANALYZE_UI_TEXT, ANALYZE_UI_TABLE, analyze_payload)

# Indexed by the firmware's screen id (0..4, plus 5 = boot STANDBY); the label
# is the 1..5 header. STANDBY is "màn 0" to the user, host id 5.
SCREEN_NAME = ['1/5 CONFIG', '2/5 TRIGGER', '3/5 WAVE CH0-3', '4/5 WAVE CH4-7',
               '5/5 DECODE', '0 STANDBY']

SCREEN_HINT = [
    'header 1/5, 4 dong gia tri + dong BAT DAU THU',
    'header 2/5, Che do / Kenh / Dieu kien / Baud-Clk',
    'header 3/5, 4 track CH0-3, vach do trigger, cursor giua',
    'header 4/5, 4 track CH4-7, cung 4 mau, khong con trong',
    'header 5/5, < UART >, dong decode ben duoi duong ke',
    'man cho, logo xung vuong, nhan nut bat ky de vao config',
]

# (key, expected screen, note)
# The demo drops the board on screen 3 (WAVE CH0-3), so the walk starts there.
STEPS = [
    ('o', 3, 'OK   -> WAVE CH4-7'),
    ('o', 4, 'OK   -> DECODE'),
    ('b', 3, 'BACK -> WAVE CH4-7'),
    ('b', 2, 'BACK -> WAVE CH0-3'),
    ('b', 0, 'BACK -> CONFIG'),
    ('b', 1, 'BACK -> TRIGGER'),
    ('b', 0, 'BACK -> CONFIG'),
]


def drain(fd, seconds=0.4):
    """Throw away frames a previous host session left queued."""
    end = time.time() + seconds
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                os.read(fd, 4096)
            except OSError:
                return


def ask(dev, op, tries=16):
    """Send one ANALYZE op and return the RESULT payload that answers it.

    A capture in flight streams SAMPLES frames that arrive between our request
    and its reply, so the reply must be matched by sequence number. Matching on
    the frame kind alone hands back a stale RESULT and looks like a firmware
    bug when it is really a queue that has not drained.
    """
    for _ in range(tries):
        want = dev.tx_seq
        kind, seq, payload = dev.command(ANALYZE, analyze_payload(op, 0, 0))
        if kind == 0x93 and seq == want:
            return payload
    raise RuntimeError('no RESULT for op %d' % op)


def runtime(dev):
    payload = ask(dev, ANALYZE_RUNTIME)
    if len(payload) != 32:
        raise RuntimeError('RUNTIME returned %d bytes' % len(payload))
    return payload


def screen_id(dev):
    return runtime(dev)[30]


def to_config(dev, fd):
    """BACK walks CONFIG <-> TRIGGER, so poll instead of assuming a parity."""
    for _ in range(6):
        if screen_id(dev) == 0:
            return True
        os.write(fd, b'b')
        time.sleep(0.3)
    return False


def show(dev, want):
    got = None
    try:
        got = screen_id(dev)
    except Exception:
        pass
    mark = '' if got is None else ('  [device %s]' % (
        'OK' if got == want else 'SAI: man %d' % got))
    print('%-14s %s%s' % (SCREEN_NAME[want], SCREEN_HINT[want], mark), flush=True)
    return got == want


def decode_text(dev):
    return ask(dev, ANALYZE_UI_TEXT).decode('utf-8', 'replace')


def decode_table(dev):
    """The grid rows the panel is drawing: "NN|0xVV|ASC|+dt" per line."""
    return ask(dev, ANALYZE_UI_TABLE).decode('utf-8', 'replace')


# A decode line that contains any of these is a failure, not a result.
BAD_TEXT = ('no transaction', 'no sof', 'truncated', 'extended frame',
            'needs the bit rate', 'no capture')


def to_decode(dev, fd):
    """Reach screen 5 from anywhere: BACK to CONFIG, then BAT DAU -> OK -> OK.

    BACK only walks 3<->4<->1<->2, so the OK chain from CONFIG is the only way
    onto the decode screen. The demo capture is already loaded, so the OK on
    BAT DAU THU just re-enters the waveform screens.
    """
    for _ in range(10):
        if screen_id(dev) == 4:
            return True
        # Reloading the demo is the only way back onto the waveform screens
        # without pressing OK on BAT DAU, which would arm a real capture and
        # throw the demo data away.
        dev.command(ANALYZE, analyze_payload(ANALYZE_UI_DEMO, 0, 0))
        time.sleep(0.6)
        os.write(fd, b'oo')            # 3/5 -> 4/5 -> 5/5
        time.sleep(0.5)
    return screen_id(dev) == 4


def select_protocol(dev, fd, name):
    """Cycle the protocol card until the decode text starts with `name`."""
    for _ in range(5):
        if decode_text(dev).strip().startswith(name):
            return True
        os.write(fd, b'odo')          # OK edit, DOWN next protocol, OK apply
        time.sleep(0.6)
    return decode_text(dev).strip().startswith(name)


def check_protocols(dev, fd):
    """Cycle the decode screen through UART/I2C/SPI/CAN and print each result."""
    print('\n-- decode screen, all four protocols --', flush=True)
    if not to_decode(dev, fd):
        print('could not reach screen 5, aborting', flush=True)
        return False
    ok = True
    for name in ['UART', 'I2C', 'SPI', 'CAN']:
        # proto_sel survives a demo reload, so cycle to the wanted protocol
        # instead of assuming where the previous run left it.
        if not select_protocol(dev, fd, name):
            print('%-4s could not select this protocol' % name, flush=True)
            ok = False
            continue
        got = screen_id(dev)
        text = decode_text(dev).strip()
        table = decode_table(dev).strip()
        low = text.lower()
        bad = [b for b in BAD_TEXT if b in low]
        rows = [r for r in table.splitlines() if r.strip()]
        good = got == 4 and bool(text) and not bad and bool(rows)
        ok = ok and good
        why = '' if good else ('  <-- %s' % (', '.join(bad) or 'no rows'))
        print('%-4s [device %s]  %s%s'
              % (name, 'OK' if got == 4 else 'man %d' % got,
                 text.replace('\n', ' | ') or '(empty)', why), flush=True)
        for row in rows:
            print('       %s' % row, flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('-i', '--interval', type=float, default=3.0,
                    help='seconds per screen (default 3)')
    ap.add_argument('--rounds', type=int, default=1,
                    help='number of laps, 0 = forever (default 1)')
    ap.add_argument('--capture', action='store_true',
                    help='arm a real capture instead of loading the mock waveform')
    ap.add_argument('--protocols', action='store_true',
                    help='cycle the decode screen through all four protocols')
    args = ap.parse_args()

    fd = open_port(args.port)
    dev = host_la.Device.__new__(host_la.Device)
    dev.fd, dev.parser, dev.tx_seq, dev.rx_seq, dev.pending = (
        fd, host_la.Parser(), 0, 0, [])
    passed = True
    try:
        drain(fd)
        dev.hello()
        lap = 0
        while args.rounds == 0 or lap < args.rounds:
            if args.capture:
                if not to_config(dev, fd):
                    print('could not reach screen 01, aborting', flush=True)
                    return 1
                os.write(fd, b'ddddo')     # 4 DOWN to BAT DAU, then OK
            else:
                dev.command(ANALYZE, analyze_payload(ANALYZE_UI_DEMO, 0, 0))
            time.sleep(1.0)
            passed &= show(dev, 2)   # the demo opens on 3/5 WAVE CH0-3
            for key, want, note in STEPS:
                print('             %s' % note, flush=True)
                time.sleep(args.interval)
                os.write(fd, key.encode())
                time.sleep(0.4)
                passed &= show(dev, want)
            if args.protocols:
                passed &= check_protocols(dev, fd)
            lap += 1
        print('\n%s; the board is on screen %s'
              % ('PASS' if passed else 'FAIL', SCREEN_NAME[screen_id(dev) or 0]),
              flush=True)
        return 0 if passed else 1
    except KeyboardInterrupt:
        print('\nstopped by hand', flush=True)
        return 0
    finally:
        os.close(fd)


if __name__ == '__main__':
    sys.exit(main())
