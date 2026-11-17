#!/usr/bin/env python3
"""Run the remaining checks from docs/PLAN_Nov16.md in one go.

    python3 tools/plan_checks.py --la /dev/ttyACM0 --gen /dev/ttyUSB0

Sections:
  can    boundary repeats (5x) + 20 kbps low-rate repeat (3x)
  ch7    the CH7 trigger timeout from PLAN §7 (burst off vs burst on)
  ui     drive the on-device UI over USB and print what each screen should show
  pv     libsigrok capture + UART/SPI/I2C/CAN decode via sigrok-cli

Only reports; no source is modified.
"""
import argparse
import os
import re
import subprocess
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import host_la  # noqa: E402

REPO = os.path.dirname(HERE)
SIGROK = os.path.join(REPO, '.local', 'bin', 'sigrok-cli')
LOCAL_LIB = os.path.join(REPO, '.local', 'lib')
CAN_OK = re.compile(r'CAN ID=0x123 DLC=4 DATA=[0-9A-F]{2}( [0-9A-F]{2}){3}')


def say(name, ok, detail=''):
    print('%-10s %s %s' % (name, 'PASS' if ok else 'FAIL', detail))


class LA:
    def __init__(self, path):
        self.path = path
        self.dev = host_la.Device(path)

    def recover(self):
        """HELLO clears a busy/desynced capture."""
        try:
            self.dev.close()
        except OSError:
            pass
        self.dev = host_la.Device(self.path)
        return self.dev.hello()

    def capture(self, rate, count, pre, ch, edge, timeout=20):
        """pre=None means a manual capture (no trigger)."""
        trig = None if pre is None else (host_la.TRIGGER_EDGE, ch, edge, 0, 0)
        self.dev.configure(rate, count, 0xff, pre or 0, trig)
        self.dev.arm()
        return self.dev.capture(timeout)


def can_line(raw, baud):
    text = raw.decode('utf-8', 'replace')
    for ln in text.splitlines():
        if ln.startswith('CAN'):
            return ln.strip()
    return '(no CAN line)'


def section_can(la, gen):
    print('\n== CAN boundary repeats')
    gen.write(b'burst off\n'); time.sleep(0.3)
    cases = [('125k @25m', 125, 25000000, 40000, 2000),
             ('1M   @25m', 1000, 25000000, 40000, 2000),
             ('500k @5m ', 500, 5000000, 40000, 2000),
             ('20k  @1m ', 20, 1000000, 262144, None)]
    for name, kbps, rate, count, pre in cases:
        gen.write(b'set can %d\n' % kbps); gen.flush(); time.sleep(0.6)
        gen.reset_input_buffer()
        rep = 3 if kbps == 20 else 5
        ok = 0
        last = ''
        for _ in range(rep):
            try:
                s, _info = la.capture(rate, count, pre, 7,
                                      host_la.EDGE_FALLING)
                last = can_line(la.dev.decode_all(115200, kbps * 1000), kbps)
                ok += bool(CAN_OK.search(last))
            except Exception as exc:
                last = 'ERROR ' + str(exc)[:40]
                la.recover()
        say(name, ok == rep, '%d/%d %s' % (ok, rep, last[:60]))


def section_ch7(la, gen):
    print('\n== CH7 trigger timeout (PLAN section 7)')
    for burst in (b'burst off\n', b'burst on\n'):
        gen.write(burst); time.sleep(0.6); gen.reset_input_buffer()
        gen.write(b'set can 250\n'); gen.flush(); time.sleep(0.6)
        gen.reset_input_buffer()
        # The manual case runs at 1 MHz: a 262144 sample window is 262 ms then,
        # wide enough to hold a CAN burst (one frame every 100 ms) without a
        # trigger. At 25 MHz the same window is 10.5 ms and usually empty.
        for rate, count, pre in ((25000000, 40000, 2000),
                                 (25000000, 100000, 2000),
                                 (1000000, 262144, None)):
            try:
                s, info = la.capture(rate, count, pre, 7,
                                     host_la.EDGE_FALLING, timeout=15)
                line = can_line(la.dev.decode_all(115200, 250000), 250000)
                say('%s N=%d @%s pre=%s' % (burst.strip().decode(), count,
                                           '25m' if rate == 25000000 else '1m', pre),
                    bool(CAN_OK.search(line)), line[:55])
            except Exception as exc:
                say('%s N=%d @%s pre=%s' % (burst.strip().decode(), count,
                                           '25m' if rate == 25000000 else '1m', pre),
                    False, 'ERROR ' + str(exc)[:45])
                la.recover()


UI_STEPS = [
    ('O', 'CONFIG: hold OK starts a capture'),
    ('b', 'screen 02 TRIGGER CONFIG'),
    ('b', 'screen 03 WAVEFORM, 4 tracks'),
    ('b', 'screen 04 DECODE, UART I2C / SPI CAN grid'),
    ('u', 'DECODE: next bus selected'),
    ('o', 'DECODE: decode again from current buffer'),
]


def section_ui(port):
    print('\n== UI over USB (needs eyes on the TFT)')
    fd = host_la.open_port(port)
    for keys, what in UI_STEPS:
        os.write(fd, keys.encode())
        time.sleep(0.8)
        print('  send %-3s -> %s' % (keys, what))
    os.close(fd)
    print('  Confirm each step on the screen, then record PASS/FAIL by hand.')


def section_pv(la, port):
    print('\n== PulseView / libsigrok path')
    env = dict(os.environ, LD_LIBRARY_PATH=LOCAL_LIB)
    # capture triggered on UART start bit with the generator in burst mode
    sr = '/tmp/opencode/plan-pv.sr'
    cmd = [SIGROK, '-d', 'rp2350-la:conn=' + port,
           '--config', 'samplerate=25m:triggersource=CH0:triggerslope=falling'
                       ':horiz_triggerpos=0.02',
           '--samples', '100000', '-o', sr]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
    ok = r.returncode == 0 and os.path.exists(sr)
    say('capture', ok, (r.stderr or r.stdout).strip()[-70:])
    if not ok:
        return
    la.recover()

    decoders = [
        ('uart', 'uart:baudrate=115200:rx=CH0', 'uart=rx_data', '4C'),
        ('spi', 'spi:clk=CH4:mosi=CH5:cs=CH3', 'spi=mosi-data', 'DE'),
        ('i2c', 'i2c:scl=CH1:sda=CH2', 'i2c=address-write', '68'),
    ]
    for name, dec, ann, want in decoders:
        r = subprocess.run([SIGROK, '-i', sr, '-P', dec, '-A', ann],
                           env=env, capture_output=True, text=True, timeout=120)
        out = r.stdout
        say(name, want in out, out.splitlines()[0][:60] if out else
            (r.stderr or '').strip()[:60])

    # CAN needs a longer window than a 4 ms UART trigger: sample 1 MHz so the
    # 262144 sample buffer covers 262 ms and holds a whole burst.
    sr = '/tmp/opencode/plan-pv-can.sr'
    cmd = [SIGROK, '-d', 'rp2350-la:conn=' + port,
           '--config', 'samplerate=1m', '--samples', '262144', '-o', sr]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
    if r.returncode != 0 or not os.path.exists(sr):
        say('can-capture', False, (r.stderr or r.stdout).strip()[-60:])
        return
    r = subprocess.run([SIGROK, '-i', sr, '-P',
                        'can:can_rx=CH7:nominal_bitrate=250000',
                        '-A', 'can=id'], env=env, capture_output=True,
                       text=True, timeout=120)
    ok = '123' in r.stdout
    say('can', ok, (r.stdout.splitlines() or [''])[0][:60] if ok else
        (r.stderr or '').strip()[:60])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--la', default='/dev/ttyACM0')
    ap.add_argument('--gen', default='/dev/ttyUSB0')
    ap.add_argument('--only', choices=('can', 'ch7', 'ui', 'pv', 'all'),
                    default='all')
    args = ap.parse_args()

    gen = None
    la = None
    if args.only in ('can', 'ch7', 'all'):
        gen = serial.Serial(args.gen, 115200, timeout=0.2)
        time.sleep(0.4)
    if args.only != 'ui':
        la = LA(args.la)
        print('board:', la.dev.hello())

    try:
        if args.only in ('can', 'all'):
            section_can(la, gen)
        if args.only in ('ch7', 'all'):
            section_ch7(la, gen)
        if args.only in ('ui', 'all'):
            section_ui(args.la)
        if args.only in ('pv', 'all'):
            section_pv(la, args.la)
    finally:
        if gen:
            gen.close()
        if la:
            la.dev.close()


if __name__ == '__main__':
    main()
