#!/usr/bin/env python3
"""Run the 8-channel limit sweep from docs/TEST-PLAN-8CH.md.

Drives the ESP32 generator console (set uart/i2c/spi/can) and the RP2350 LA
(trigger + capture + on-device decode), records PASS/FAIL per case.

    python3 tools/sweep.py --la /dev/ttyACM0 --gen /dev/ttyUSB0
    python3 tools/sweep.py --la /dev/ttyACM0 --gen /dev/ttyUSB0 --only uart
"""
import argparse
import re
import sys
import time

import serial

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import host_la  # noqa: E402

RATES = {'1k': 1000, '10k': 10000, '100k': 100000, '250k': 250000,
         '500k': 500000, '1m': 1000000, '5m': 5000000, '25m': 25000000}

EDGE = {'uart': 0, 'i2c': 1, 'spi': 3, 'can': 7}
EDGE_LEVEL = {'uart': 'fall', 'i2c': 'rise', 'spi': 'fall', 'can': 'fall'}

# (value, rate, count, pre) -- pre=None means manual capture (no trigger)
UART_CASES = [
    (9600, '1m', 10000, 500), (19200, '1m', 5000, 200),
    (38400, '1m', 5000, 200), (57600, '1m', 4000, 100),
    (115200, '25m', 40000, 2000), (115200, '5m', 40000, 2000),
    (115200, '1m', 40000, 2000), (230400, '25m', 40000, 2000),
    (230400, '5m', 40000, 2000), (230400, '1m', 40000, 2000),
    (460800, '25m', 40000, 2000), (460800, '5m', 40000, 2000),
    (921600, '25m', 40000, 2000), (921600, '5m', 40000, 2000),
    (1000000, '25m', 40000, 2000), (1500000, '25m', 40000, 2000),
    (2000000, '25m', 40000, 2000), (3000000, '25m', 40000, 2000),
    (5000000, '25m', 40000, 2000), (115200, '25m', 262144, None),
]

I2C_CASES = [
    (100, '1m', 100000, 1000), (100, '25m', 40000, 2000),
    (100, '100k', 100000, 1000), (100, '250k', 100000, 1000),
    (100, '1m', 262144, None), (4, '1m', 20000, 1000),
    (10, '1m', 10000, 1000), (25, '1m', 10000, 500),
    (50, '1m', 10000, 500), (125, '5m', 40000, 2000),
    (150, '5m', 40000, 2000), (250, '5m', 40000, 2000),
    (250, '25m', 40000, 2000), (250, '1m', 40000, 2000),
    (500, '25m', 40000, 2000), (500, '5m', 40000, 2000),
    (100, '25m', 40000, 1), (100, '25m', 40000, 20000),
    (100, '25m', 1000, 999), (500, '25m', 40000, 2000),
]

SPI_CASES = [
    (100, '1m', 100000, 1000), (100, '25m', 40000, 2000),
    (250, '5m', 40000, 2000), (500, '5m', 40000, 2000),
    (1000, '5m', 40000, 2000), (1000, '25m', 40000, 2000),
    (2000, '25m', 40000, 2000), (3000, '25m', 40000, 2000),
    (4000, '25m', 40000, 2000), (5000, '25m', 40000, 2000),
    (6000, '25m', 40000, 2000), (8000, '25m', 40000, 2000),
    (10000, '25m', 40000, 2000), (12000, '25m', 40000, 2000),
    (16000, '25m', 40000, 2000), (20000, '25m', 40000, 2000),
    (26666, '25m', 40000, 2000), (1000, '25m', 40000, 2000),
    (1000, '1m', 262144, None), (1000, '25m', 40000, 2000),
]

CAN_CASES = [
    (125, '1m', 262144, None, 125000), (125, '25m', 40000, 2000, 125000),
    (125, '500k', 262144, None, 125000), (125, '250k', 262144, None, 125000),
    (250, '1m', 100000, 1000, 250000), (250, '5m', 40000, 2000, 250000),
    (250, '25m', 40000, 2000, 250000), (500, '1m', 100000, 1000, 500000),
    (500, '5m', 40000, 2000, 500000), (500, '25m', 40000, 2000, 500000),
    (800, '5m', 40000, 2000, 800000), (800, '25m', 40000, 2000, 800000),
    (1000, '5m', 40000, 2000, 1000000), (1000, '25m', 40000, 2000, 1000000),
    (1000, '1m', 100000, 1000, 1000000), (20, '1m', 262144, None, 20000),
    (250, '25m', 40000, 2000, 500000),  # wrong baud on purpose
    (250, '25m', 40000, 2000, 250000), (250, '25m', 20000, 5000, 250000),
    (250, '1m', 262144, None, 250000),
]

NEGATIVE = {('i2c', 17), ('i2c', 19), ('can', 17)}
REPEAT5 = ('i2c', 20)


class Gen:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(0.4)

    def cmd(self, line, wait=0.25):
        self.ser.write((line + '\n').encode())
        self.ser.flush()
        time.sleep(wait)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()


def decode_ok(proto, text):
    if proto == 'uart':
        m = re.search(r'UART (\d+) 8N1: "([^"]*)"', text)
        return bool(m and 'LA1234' in m.group(2))
    if proto == 'i2c':
        return 'I2C: 0x68 W ACK 0x01 ACK 0x5A ACK' in text
    if proto == 'spi':
        return 'SPI: DE AD BE EF 00 01 02 03' in text
    if proto == 'can':
        return bool(re.search(r'CAN ID=0x123 DLC=4 DATA=([0-9A-F]{2} ){4}', text))
    return False


def line_of(proto, text):
    tag = {'uart': 'UART', 'i2c': 'I2C', 'spi': 'SPI', 'can': 'CAN'}[proto]
    for ln in text.splitlines():
        if ln.startswith(tag):
            return ln.strip()
    return '(no line)'


class Link:
    """LA connection that can recover from a desynced / busy capture."""

    def __init__(self, path):
        self.path = path
        self.dev = host_la.Device(path)
        print('board:', self.dev.hello())
        self.dev.caps()

    def reopen(self):
        try:
            self.dev.close()
        except OSError:
            pass
        for _ in range(6):
            time.sleep(1.5)
            try:
                self.dev = host_la.Device(self.path)
                self.dev.hello()
                return
            except Exception:
                continue
        raise RuntimeError('cannot reopen ' + self.path)


def run_case(link, proto, num, value, rate, count, pre, can_baud=None,
             mask=0xff, repeats=1):
    trig = None
    if pre is not None:
        trig = (host_la.TRIGGER_EDGE, EDGE[proto],
                host_la.EDGE_FALLING if EDGE_LEVEL[proto] == 'fall'
                else host_la.EDGE_RISING, 0, 0)
    uart_baud = value if proto == 'uart' else 115200
    if proto == 'can':
        uart_baud = 115200
    cb = can_baud if can_baud is not None else 250000

    ok = 0
    last = ''
    err = ''
    for _ in range(repeats):
        for attempt in range(2):
            try:
                link.dev.configure(RATES[rate], count, mask, pre or 0, trig)
                link.dev.arm()
                samples, info = link.dev.capture(30.0)
                text = link.dev.decode_all(uart_baud, cb).decode('utf-8', 'replace')
                last = line_of(proto, text)
                if decode_ok(proto, text):
                    ok += 1
                break
            except Exception as exc:  # no trigger, desync, capture error
                err = str(exc)[:40]
                last = 'ERROR: ' + err
                if attempt == 0:
                    link.reopen()
    return ok, repeats, last


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--la', default='/dev/ttyACM0')
    ap.add_argument('--gen', default='/dev/ttyUSB0')
    ap.add_argument('--only', choices=('uart', 'i2c', 'spi', 'can', 'all'),
                    default='all')
    ap.add_argument('--out', help='write markdown results here')
    args = ap.parse_args()

    gen = Gen(args.gen)
    gen.cmd('burst off', 0.3)
    link = Link(args.la)
    dev = link.dev
    rows = []

    def report(proto, num, expected, value, rate, count, pre, ok, total, last):
        verdict = 'PASS' if ok == total else ('PART' if ok else 'FAIL')
        mark = 'ok' if verdict == 'PASS' else '**%s**' % verdict
        print('%-4s %2d  %-28s %-6s %-5s N=%-6d pre=%-6s %s | %s'
              % (proto, num, str(value), rate, expected, count, pre,
                 mark, last[:70]))
        rows.append((proto, num, str(value), rate, count, pre, expected,
                     verdict, last))

    if args.only in ('uart', 'all'):
        for i, (baud, rate, count, pre) in enumerate(UART_CASES, 1):
            gen.cmd('set uart %d' % baud)
            exp = {1: 'P', 2: 'P', 3: 'P', 4: 'P', 5: 'P', 6: 'P',
                   7: 'M', 8: 'P', 9: 'M', 10: 'P', 11: 'P', 12: 'M',
                   13: 'P', 14: 'P', 15: 'P', 16: 'M', 17: 'P', 18: 'P',
                   19: 'P', 20: 'P'}[i]
            ok, tot, last = run_case(link, 'uart', i, baud, rate, count, pre)
            report('uart', i, exp, baud, rate, count, pre, ok, tot, last)

    if args.only in ('i2c', 'all'):
        for i, (khz, rate, count, pre) in enumerate(I2C_CASES, 1):
            gen.cmd('set i2c %d' % khz)
            exp = {1: 'P', 2: 'P', 3: 'F', 4: 'M', 5: 'P', 6: 'P', 7: 'P',
                   8: 'P', 9: 'P', 10: 'P', 11: 'P', 12: 'P', 13: 'P',
                   14: 'P', 15: 'M', 16: 'M', 17: 'F', 18: 'P', 19: 'F',
                   20: 'M'}[i]
            rep = 5 if (('i2c', i) == REPEAT5) else 1
            ok, tot, last = run_case(link, 'i2c', i, khz, rate, count, pre,
                                     repeats=rep)
            report('i2c', i, exp, '%d kHz' % khz, rate, count, pre, ok, tot, last)

    if args.only in ('spi', 'all'):
        for i, (khz, rate, count, pre) in enumerate(SPI_CASES, 1):
            gen.cmd('set spi %d' % khz)
            exp = {1: 'P', 2: 'P', 3: 'P', 4: 'P', 5: 'P', 6: 'P', 7: 'P',
                   8: 'P', 9: 'P', 10: 'M', 11: 'M', 12: 'M', 13: 'F',
                   14: 'F', 15: 'F', 16: 'F', 17: 'F', 18: 'P', 19: 'P',
                   20: 'P'}[i]
            ok, tot, last = run_case(link, 'spi', i, khz, rate, count, pre)
            report('spi', i, exp, '%d kHz' % khz, rate, count, pre, ok, tot, last)

    if args.only in ('can', 'all'):
        for i, (kbps, rate, count, pre, cb) in enumerate(CAN_CASES, 1):
            gen.cmd('set can %d' % kbps, 0.5)
            exp = {1: 'P', 2: 'P', 3: 'P', 4: 'M', 5: 'P', 6: 'P', 7: 'P',
                   8: 'M', 9: 'P', 10: 'P', 11: 'M', 12: 'P', 13: 'P',
                   14: 'P', 15: 'F', 16: 'P', 17: 'F', 18: 'P', 19: 'P',
                   20: 'P'}[i]
            ok, tot, last = run_case(link, 'can', i, kbps, rate, count, pre,
                                     can_baud=cb)
            report('can', i, exp, '%d kbps' % kbps, rate, count, pre, ok, tot, last)

    link.dev.close()
    gen.close()

    if args.out:
        with open(args.out, 'w', encoding='utf-8') as f:
            f.write('# Sweep results\n\n')
            f.write('| proto | # | value | rate | N | pre | expected | verdict | decode |\n')
            f.write('|---|---|---|---|---|---|---|---|---|\n')
            for r in rows:
                f.write('| %s | %d | %s | %s | %d | %s | %s | %s | `%s` |\n'
                        % (r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                           r[8].replace('|', '/')))
        print('wrote', args.out)


if __name__ == '__main__':
    main()
