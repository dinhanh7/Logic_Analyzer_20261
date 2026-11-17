#!/usr/bin/env python3
"""Exercise actual Pico TFT controls via the physical buttons' USB event queue.

Uses a real ESP32 snapshot, never UI_DEMO. Saves the on-device framebuffer at
all important steps. No physical buttons are required; mechanical contacts
still require a separate check when soldered.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time

import capture_ui
import host_la
import lar2


def tour(dev, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    states = []
    def check(name, screen=None, image=False):
        state = capture_ui.view_state(dev)
        if screen is not None: assert state['screen'] == screen, (name,state)
        if state['count']: assert 0 <= state['start'] <= state['count'] - state['visible'], (name,state)
        states.append(dict(step=name,**state))
        if image: capture_ui.save_frame(dev,output/(name+'.png'))
        (output/'controls.json').write_text(json.dumps(states,indent=2))
        return state
    def key(k):
        os.write(dev.fd,k.encode())
        time.sleep(.08)
        return capture_ui.view_state(dev)
    dev.analyze(lar2.ANALYZE_UI_SHOW_CAPTURE,2,1)
    initial = check('wave3-initial',2,True)
    assert initial['rate'] == 25000000 and initial['count'] == 65536
    pan = key('d'); assert pan['start'] > initial['start']
    centre = pan['start'] + pan['visible']//2
    zoom = key('U'); assert zoom['visible'] <= pan['visible']
    assert abs(zoom['start'] + zoom['visible']//2-centre) <= 1
    lo = check('wave3-zoom-in',2,True)
    key('o'); hi0=check('wave4-initial',3,True)
    key('d'); key('U'); hi=check('wave4-zoom-in',3,True)
    key('o'); check('decode',4,True)
    key('b'); back=check('wave4-restored',3)
    assert (back['start'],back['zoom']) == (hi['start'],hi['zoom'])
    key('b'); back=check('wave3-restored',2)
    assert (back['start'],back['zoom']) == (lo['start'],lo['zoom'])
    for _ in range(9): key('D')
    fit=check('wave3-fit',2,True)
    assert fit['visible'] == fit['count'] and fit['start'] == 0
    key('d'); key('u'); assert capture_ui.view_state(dev)['start'] == 0
    key('U')
    for _ in range(12): key('u')
    assert capture_ui.view_state(dev)['start'] == 0
    for _ in range(12): key('d')
    end=check('wave3-pan-end',2,True); assert end['start']+end['visible'] == end['count']
    key('O'); reset=check('wave3-reset',2)
    assert (reset['start'],reset['zoom']) == (initial['start'],initial['zoom'])
    key('b');check('config',0,True)
    # Edit rate and restore it; ensure navigation enters/exits edit mode.
    key('o');key('d');key('u');key('o')
    key('b');check('trigger',1,True)
    key('o');key('d');key('u');key('o');key('b')
    key('B');check('standby',5,True)
    key('o');check('wake-config',0)
    # Hold OK uses the same capture request as a real long press. Drain every
    # SAMPLES frame immediately, then restart the command sequence with HELLO.
    os.write(dev.fd,b'O')
    samples, info=dev.capture(timeout=20)
    assert len(samples) == 65536 and info['rate'] == 25000000
    dev.recover()
    check('button-capture-complete',2,True)
    return dict(status='PASS',steps=len(states),initial_lo=initial,initial_hi=hi0,button_capture=info)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',default=host_la.default_port())
    ap.add_argument('--output',type=Path,default=Path('build/ui-hardware')/time.strftime('%Y%m%d-%H%M%S'))
    args=ap.parse_args()
    dev=host_la.Device(args.port)
    try:
        print(dev.hello())
        dev.configure(25000000,65536,255,32768,(1,0,1,0,255))
        dev.arm();dev.capture(timeout=20)
        result=tour(dev,args.output)
        (args.output/'summary.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(result,indent=2))
    finally:dev.close()
    return 0


if __name__=='__main__':sys.exit(main())
