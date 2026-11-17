#!/usr/bin/env python3
"""Check actual flash round trips and byte-exact golden/diff on an ERASED slot.

Occupied slots are never overwritten. A test session is retained in the
previously erased slot; this is not an endurance or power-loss test.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time
import zlib
import host_la
import lar2


def replay(dev):
    os.write(dev.fd,lar2.frame(lar2.ANALYZE,dev.tx_seq,lar2.analyze_payload(lar2.ANALYZE_REPLAY)))
    dev.tx_seq=(dev.tx_seq+1)&65535
    return dev.capture()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',default=host_la.default_port())
    ap.add_argument('--output',type=Path,default=Path('build/storage-test')/time.strftime('%Y%m%d-%H%M%S'))
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    dev=host_la.Device(args.port); result={'status':'RUNNING','cases':[]}
    try:
        dev.hello();empty=[]
        for slot in (1,0):
            header=dev.analyze(lar2.ANALYZE_SESSION,slot,strip_status=False)
            (args.output/f'original-slot{slot}.bin').write_bytes(header)
            if header==b'\xff'*len(header):empty.append(slot)
        if not empty:
            result.update(status='SKIP',reason='No erased slot; user sessions preserved')
            print(result);return 2
        slot=empty[0];result['slot']=slot
        for count in (1,3,1031,262144):
            dev.configure(1000000,count,255);dev.arm();expected,info=dev.capture()
            dev.save_session(slot);dev.save_golden()
            assert dev.compare_golden().startswith(b'SIG: MATCH')
            assert dev.diff_session(slot).startswith(b'DIFF: identical')
            assert dev.compare_golden().startswith(b'SIG: MATCH'),'diff changed the golden reference'
            dev.configure(1000000,17,255);dev.arm();dev.capture()
            assert dev.diff_session(slot).startswith(b'DIFF: first mismatch')
            dev.load_session(slot);got,restored=replay(dev)
            assert got==expected and restored['count']==count and restored['rate']==info['rate']
            assert dev.compare_golden().startswith(b'SIG: MATCH')
            result['cases'].append(dict(count=count,crc32=f'{zlib.crc32(got):08x}',status='PASS'))
            (args.output/'summary.json').write_text(json.dumps(result,indent=2))
        result['status']='PASS'
    finally:
        dev.close();(args.output/'summary.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2));return 0


if __name__=='__main__':sys.exit(main())
