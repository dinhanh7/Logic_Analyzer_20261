#!/usr/bin/env python3
"""Measure real USB streaming with normal, dense and deliberately stalled input.

Every case retains its .sr, strict counters, time/RSS log, and console trace.
--seconds 60 is the default. A stalled run must FAIL its integrity check;
normal 1 MHz RLE and raw 500 kHz must PASS. Dense RLE / raw 1 MHz are
capacity probes; any drops remain explicitly INVALID, never a lossless PASS.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile

import host_la
ROOT = Path(__file__).resolve().parents[1]


class Generator:
    def __init__(self, port):
        import serial
        self.ser = serial.Serial(port=None, baudrate=115200, timeout=0.05)
        self.ser.dtr = self.ser.rts = False
        self.ser.port = port
        self.ser.open()
        # CP2102 reset-line transitions can reboot ESP32 on open. Keep one
        # console open throughout the benchmark and wait for app_main first.
        time.sleep(1)
        self.boot = self.ser.read(32768).decode('utf-8', 'replace')

    def command(self, text):
        self.ser.write((text + '\n').encode())
        self.ser.flush()
        data = bytearray()
        until = time.monotonic() + .4
        while time.monotonic() < until:
            data.extend(self.ser.read(4096))
        reply = data.decode('utf-8', 'replace')
        if '> ' + text not in reply:
            raise RuntimeError('ESP32 did not acknowledge command: '+text+' '+reply)
        return reply

    def close(self):
        self.ser.close()


def square_metrics(path, rate):
    # First 100 ms only; input must have >=100 periods. Reject missing edges.
    with zipfile.ZipFile(path) as z:
        with z.open('logic-1-1') as f:
            data = f.read(rate // 10)
    rises = [i for i in range(1, len(data)) if (data[i] & 1) and not (data[i-1] & 1)]
    if len(rises) < 100:
        raise RuntimeError('square wave reference missing on CH0')
    periods = [b-a for a, b in zip(rises, rises[1:])]
    duty = sum(v & 1 for v in data[rises[0]:rises[-1]]) / (rises[-1] - rises[0])
    return dict(edges=len(rises), frequency_hz=rate * (len(rises)-1)/(rises[-1]-rises[0]),
                duty=duty, period_samples_min=min(periods), period_samples_max=max(periods))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default=host_la.default_port())
    ap.add_argument('--esp32-port', default=os.environ.get('ESP32_PORT', '/dev/ttyUSB0'))
    ap.add_argument('--seconds', type=float, default=60)
    ap.add_argument('--only', choices=('all', 'dense'), default='all')
    ap.add_argument('--output', type=Path, default=ROOT/'build'/'stream-benchmark'/time.strftime('%Y%m%d-%H%M%S'))
    args = ap.parse_args()
    if args.seconds < 3:
        ap.error('use at least 3 seconds')
    args.output.mkdir(parents=True, exist_ok=False)
    generator = Generator(args.esp32_port)
    transcript, results = [generator.boot], []
    def gen(text):
        reply = generator.command(text)
        transcript.append(text + '\n' + reply)
        (args.output/'esp32.log').write_text('\n'.join(transcript))
        return reply
    cases = [('normal-rle1m-1', 1000000, True, 0, 0, True),
             ('normal-rle1m-2', 1000000, True, 0, 0, True),
             ('normal-rle1m-3', 1000000, True, 0, 0, True),
             ('normal-raw500k', 500000, False, 0, 0, True),
             ('dense-rle1m', 1000000, True, 250000, 0, None),
             ('dense-raw500k', 500000, False, 125000, 0, True),
             ('capacity-raw1m', 1000000, False, 250000, 0, None),
             ('host-stall', 500000, False, 125000, .8, False)]
    if args.only == 'dense': cases = cases[4:]
    try:
        gen('stress off'); gen('uart on'); gen('set uart 115200'); gen('set i2c 100')
        gen('set spi 100'); gen('set can 250'); gen('burst on')
        for name, rate, rle, hz, pause, expect in cases:
            gen('stress %d' % hz if hz else 'stress off')
            path = args.output/(name+'.sr')
            cmd = ['/usr/bin/time','-v',sys.executable,str(ROOT/'tools/stream_capture.py'),
                   '--port',args.port,'-r',str(rate),'-t',str(args.seconds),'-o',str(path)]
            if not rle: cmd += ['--no-rle']
            if pause: cmd += ['--pause-seconds',str(pause)]
            run = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=args.seconds+40)
            (args.output/(name+'.log')).write_text(run.stdout+'\n'+run.stderr)
            meta = json.loads(path.with_suffix('.json').read_text())
            rss = re.search(r'Maximum resident set size \(kbytes\): (\d+)',run.stderr)
            meta.update(name=name, exit_code=run.returncode, expected_valid=expect,
                        host_peak_rss_kib=int(rss[1]) if rss else None)
            if hz and meta['received_samples']:
                meta['square'] = square_metrics(path,rate)
                if abs(meta['square']['frequency_hz']/hz-1)>.01 or abs(meta['square']['duty']-.5)>.01:
                    raise RuntimeError('reference square frequency/duty failed: '+str(meta['square']))
            meta['pass'] = (expect is None or meta['valid'] == expect) and ((run.returncode == 0) == meta['valid'])
            if pause: meta['pass'] &= meta['dropped'] > 0
            results.append(meta)
            (args.output/'summary.json').write_text(json.dumps(results,indent=2))
            verdict = ('LOSSLESS' if meta['valid'] else 'EXPECTED_STALL_LOSS' if pause else 'CAPACITY_LIMIT') if meta['pass'] else 'FAIL'
            meta['verdict'] = verdict
            (args.output/'summary.json').write_text(json.dumps(results,indent=2))
            print(name, verdict, 'valid=',meta['valid'],
                  'received=',meta['received_samples'],'drop=',meta['dropped'],flush=True)
            if not meta['pass']: raise RuntimeError('benchmark integrity expectation failed: '+name)
    finally:
        gen('stress off'); gen('set spi 1000'); gen('set can 250'); gen('uart on'); gen('burst on'); gen('status')
        generator.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
