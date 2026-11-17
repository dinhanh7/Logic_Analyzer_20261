#!/usr/bin/env python3
"""Regression-check ESP32 console and generated signals using the wired Pico."""
import argparse
import json
from pathlib import Path
import re
import sys
import time

import host_la
from stream_benchmark import Generator, square_metrics
from stream_capture import write_sr


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',default=host_la.default_port())
    ap.add_argument('--esp32-port',default='/dev/ttyUSB0')
    ap.add_argument('--output',type=Path,default=Path('build/generator-test')/time.strftime('%Y%m%d-%H%M%S'))
    args=ap.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    gen=Generator(args.esp32_port); dev=host_la.Device(args.port)
    log=[gen.boot]; result={'status':'RUNNING'}
    def cmd(text):
        reply=gen.command(text);log.append(reply)
        (args.output/'console.log').write_text('\n'.join(log))
        return reply
    def capture(name):
        dev.recover();dev.configure(1000000,262144,255);dev.arm();data,info=dev.capture()
        write_sr(args.output/(name+'.sr'),data,info['rate']);return data
    try:
        dev.hello()
        for c in ('stress off','set uart 115200','set i2c 100','set spi 1000','set can 250','burst on'):cmd(c)
        initial=cmd('status')
        for c in ('freq 0','set can 10','set can 999junk','set spi 0','set uart 42949672960'):
            reply=cmd(c)
            assert any(t in reply for t in ('preserved','invalid','100..26666')),reply
        status=cmd('status');assert '@250 kbps' in status and '1000000 Hz, frame' in status
        cmd('uart off');data=capture('uart-off')
        assert all(v&1 for v in data),'uart off must also silence the burst task'
        result['uart_off_samples']=len(data)
        cmd('stress 1000');capture('square1k')
        metrics=square_metrics(args.output/'square1k.sr',1000000)
        assert abs(metrics['frequency_hz']/1000-1)<.005 and abs(metrics['duty']-.5)<.005,metrics
        result['square1k']=metrics
        cmd('stress off');cmd('uart on')
        for _ in range(5):cmd('set spi 100');cmd('set spi 1000')
        # The old console discarded input whenever a human paused >200 ms.
        for ch in 'set uart 115200\n':
            gen.ser.write(ch.encode());gen.ser.flush();time.sleep(.25)
        slow=gen.ser.read(8192).decode('utf-8','replace');log.append(slow)
        assert '> set uart 115200' in slow and 'uart: 115200 baud' in slow,slow
        gen.ser.write(('x'*160+'\nstatus\n').encode());gen.ser.flush();time.sleep(.5)
        long=gen.ser.read(8192).decode('utf-8','replace');log.append(long)
        assert 'command too long' in long and 'status:' in long,long
        final=cmd('status')
        assert 'errors=0, SPI errors=0' in final,final
        sent=lambda text:int(re.search(r'CAN queued=(\d+)',text)[1])
        assert sent(final)>sent(initial),'CAN generator stopped or rebooted'
        result.update(status='PASS',can_queued_before=sent(initial),can_queued_after=sent(final),
                      invalid_commands=5,spi_reconfigurations=10,slow_command='PASS',oversized_line='PASS')
    finally:
        try:
            for c in ('stress off','uart on','set spi 1000','set can 250','burst on'):cmd(c)
        finally:
            (args.output/'console.log').write_text('\n'.join(log))
            (args.output/'summary.json').write_text(json.dumps(result,indent=2))
            gen.close();dev.close()
    print(json.dumps(result,indent=2));return 0


if __name__=='__main__':sys.exit(main())
