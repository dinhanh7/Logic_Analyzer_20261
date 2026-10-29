#!/usr/bin/env python3
"""PTY device emulator. Exercises the actual libsigrok serial driver on Linux."""
import argparse
import errno
import os
import pty
import select
import struct
import threading
import time
import tty
import zlib
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lar2 import *

class Emulator:
    def __init__(self,mode='counter',fault=None,fragment=0):
        self.master,self.slave=pty.openpty(); tty.setraw(self.slave)
        self.path=os.ttyname(self.slave)
        os.set_blocking(self.master,False)
        self.mode,self.fault,self.fragment=mode,fault,fragment
        self.stop_event=threading.Event(); self.parser=Parser()
        self.seq=0; self.expected=0; self.config=None; self.pending=[]
        self.error=None; self.thread=threading.Thread(target=self.run,daemon=True)
    def __enter__(self): self.thread.start(); return self
    def __exit__(self,*args):
        self.stop_event.set(); self.thread.join(3)
        os.close(self.master); os.close(self.slave)
        if self.error and args[0] is None: raise self.error
    def queue(self,kind,payload=b''):
        self.pending.append(frame(kind,self.seq,payload)); self.seq=(self.seq+1)&0xffff
    def command(self,kind,seq,payload):
        if kind==HELLO and seq==0 and not payload:
            self.pending.clear(); self.seq=0; self.expected=1; self.config=None
            self.queue(ID,b'RP2350-LA/emulator-1.0'); return
        if seq!=self.expected:
            self.queue(ERROR,b'sequence'); return
        self.expected=(self.expected+1)&0xffff
        if kind==GET_CAPS:
            self.queue(CAPS,struct.pack('<BBHII',8,1,0,MAX_SAMPLES,len(RATES))+struct.pack('<8Q',*RATES))
        elif kind==CONFIG:
            if len(payload) not in (20,28): self.queue(ERROR,b'config length'); return
            if len(payload)==20:
                rate,count,pre,mask,trigger,reserved=struct.unpack('<QIIBBH',payload)
                if rate not in RATES or not 1<=count<=MAX_SAMPLES or pre or not mask or trigger or reserved:
                    self.config=None; self.queue(ERROR,b'config'); return
                self.config=rate,count,mask,0,None
            else:
                (rate,count,pre,mask,mode,channel,edge,value,pattern_mask,
                 reserved,reserved2)=struct.unpack('<QIIBBBBBBHI',payload)
                if rate not in RATES or not 1<=count<=MAX_SAMPLES or not mask or reserved or reserved2:
                    self.config=None; self.queue(ERROR,b'config'); return
                if mode==0:
                    if pre: self.config=None; self.queue(ERROR,b'config'); return
                    trigger=None
                elif mode==1:
                    if channel>7 or edge>1 or pre>=count:
                        self.config=None; self.queue(ERROR,b'config'); return
                    trigger=('edge',channel,edge,0,0)
                elif mode==2:
                    if channel>7 or pre>=count:
                        self.config=None; self.queue(ERROR,b'config'); return
                    trigger=('pattern',channel,0,value,pattern_mask)
                else:
                    self.config=None; self.queue(ERROR,b'config'); return
                self.config=rate,count,mask,pre,trigger
            self.queue(ACK,bytes([kind]))
        elif kind==ABORT:
            self.pending.clear(); self.config=None; self.queue(ACK,bytes([kind]))
        elif kind==ARM and self.config:
            self.queue(ACK,bytes([kind]))
            rate,count,mask,pre,trigger=self.config
            if self.fault=='wait': return
            data=pattern(count,rate,self.mode)
            trigger_index=0xffffffff
            if trigger is not None:
                # Mirror the firmware: the trigger is only accepted once the
                # pre-trigger buffer is full, and the delivered window is
                # [T-pre, T-pre+count) so the trigger sits at offset pre.
                stream=pattern(pre+count+count,rate,self.mode)
                hit=find_trigger(stream,pre,pre+count,trigger)
                if hit is None:
                    self.queue(ERROR,b'no trigger'); return
                trigger_index=pre
                data=stream[hit-pre:hit-pre+count]
            self.queue(INFO,struct.pack('<QIII4B',rate+(self.fault=='metadata'),count,trigger_index,pre,8,1,mask,0))
            for i in range(0,count,MAX_PAYLOAD): self.queue(SAMPLES,data[i:i+MAX_PAYLOAD])
            self.queue(END,struct.pack('<II',count,zlib.crc32(data)^(self.fault=='capture-crc')))
            if self.fault=='crc':
                bad=bytearray(self.pending[2]); bad[-1]^=1; self.pending[2]=bytes(bad)
            elif self.fault=='sequence':
                self.pending[2]=frame(SAMPLES,0x3456,data[:MAX_PAYLOAD])
            elif self.fault=='truncated': self.pending=self.pending[:2]
        elif kind==ANALYZE:
            # Minimal ANALYZE support for emulation
            self.queue(RESULT, bytes([RESULT_OK]))
        else: self.queue(ERROR,b'command')
    def run(self):
        outgoing=b''
        try:
            while not self.stop_event.is_set():
                readable,writable,_=select.select([self.master],[self.master] if outgoing or self.pending else [],[],0.02)
                if readable:
                    try: data=os.read(self.master,4096)
                    except OSError as e:
                        if e.errno in (errno.EIO,errno.EAGAIN): continue
                        raise
                    for kind,seq,payload in self.parser.feed(data):
                        if kind in (HELLO,ABORT): outgoing=b''
                        self.command(kind,seq,payload)
                if writable:
                    if not outgoing and self.pending: outgoing=self.pending.pop(0)
                    if outgoing:
                        try:
                            n=os.write(self.master,outgoing[:self.fragment] if self.fragment else outgoing)
                            outgoing=outgoing[n:]
                        except BlockingIOError: pass
        except Exception as exc: self.error=exc

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pattern',choices=['counter','square','uart'],default='square')
    ap.add_argument('--fault',choices=['crc','sequence','metadata','capture-crc','truncated','wait'])
    ap.add_argument('--fragment',type=int,default=0)
    args=ap.parse_args()
    with Emulator(args.pattern,args.fault,args.fragment) as device:
        print(device.path,flush=True)
        print('Connect using scripts/run-pulseview.sh '+device.path,flush=True)
        try:
            while not device.error: time.sleep(0.2)
        except KeyboardInterrupt: pass
