#!/usr/bin/env python3
"""RP2350 QA v2: independent offline and real-bench acceptance evidence."""
import argparse
from collections import Counter
from pathlib import Path
import sys
import time
from qa_common import Context,ROOT
import host_la


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--offline',action='store_true')
    ap.add_argument('--port',default=host_la.default_port())
    ap.add_argument('--esp32-port',default='/dev/ttyUSB0')
    ap.add_argument('--soak-seconds',type=int,default=300)
    ap.add_argument('--output',type=Path,default=ROOT/'results'/('qa-v2-'+time.strftime('%Y%m%d-%H%M%S')))
    ap.add_argument('--groups',default='A,B,C,D,E,F',help='comma-separated groups; omitted groups are not executed')
    args=ap.parse_args();ctx=Context(args)
    selected=set(args.groups.split(','))
    if 'A' in selected:
        import qa_offline;qa_offline.register(ctx)
    for group,module in [('B','qa_device'),('C','qa_streaming'),('D','qa_software'),('E','qa_hardware'),('F','qa_regression')]:
        if group not in selected:continue
        if args.offline and group not in ('E',):continue
        try:__import__(module).register(ctx)
        except ImportError as exc:
            print('Harness module unavailable:',exc,file=sys.stderr);return 1
    counts=Counter(r['status'] for r in ctx.rows)
    print('\n',dict(counts),'Evidence:',ctx.out,flush=True)
    ctx.save('run.json',dict(counts=dict(counts),exit_code=1 if counts['FAIL'] else 0,
                            complete_matrix=selected==set('ABCDEF') and not args.offline and args.soak_seconds>=300,
                            product_qualified=False if counts['BLOCKED'] or counts['NOT-VERIFIED'] or counts['FAIL'] else True))
    if not counts['FAIL'] and selected==set('ABCDEF') and not args.offline and args.soak_seconds>=300:
        (ROOT/'results/LATEST.txt').write_text(ctx.out.name+'\n')
    print('Exit 0 means no executed FAIL; BLOCKED/NOT-VERIFIED never certify the product.')
    return 1 if counts['FAIL'] else 0


if __name__=='__main__':sys.exit(main())
