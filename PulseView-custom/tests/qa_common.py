"""Shared standard-library runner and machine-readable evidence."""
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import traceback
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import host_la
from stream_capture import write_sr

class Context:
    def __init__(self,args):
        self.args=args;self.out=args.output.resolve();self.out.mkdir(parents=True,exist_ok=False)
        self.rows=[];self.bridge=ROOT/'build/qa-v2/decoder-bridge'
        paths=list((ROOT/'firmware').glob('*.[ch]'))+list((ROOT/'driver').glob('*.[ch]'))+list((ROOT/'protocol').glob('*.h'))
        paths+=list((ROOT/'tools').glob('*.py'))+list((ROOT/'tests').glob('qa_*'))+list((ROOT/'patches').glob('*.patch'))
        hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        for p in [ROOT/'build/firmware-pico2/rp2350_la.uf2',ROOT/'build/pulseview/pulseview',ROOT/'.local/lib/libsigrokdecode.so',ROOT/'.local/lib/libsigrok.so']:
            if p.exists():hashes[str(p.relative_to(ROOT))]=hashlib.sha256(p.read_bytes()).hexdigest()
        self.save('manifest.json',dict(timestamp=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
            commit=self.run(['git','rev-parse','HEAD']).stdout.strip(),
            dirty=self.run(['git','status','--porcelain']).stdout,hashes=hashes,port=args.port,
            python=sys.version,soak_seconds=args.soak_seconds,groups=args.groups,offline=args.offline))
    def run(self,cmd,timeout=60):
        return subprocess.run([str(a) for a in cmd],cwd=ROOT,text=True,capture_output=True,timeout=timeout)
    def save(self,path,value):
        p=self.out/path;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text(json.dumps(value,indent=2,ensure_ascii=False) if not isinstance(value,str) else value)
        return p
    def case(self,id,objective,conditions,expected,fn):
        before=set(self.out.rglob('*'));t=time.monotonic()
        row=dict(id=id,objective=objective,conditions=conditions,expected=expected)
        try:
            actual=fn();row.update(status='PASS',actual=actual)
        except Blocked as exc:row.update(status=exc.status,actual=str(exc))
        except Exception as exc:
            row.update(status='FAIL',actual=str(exc));self.save(id+'-exception.log',traceback.format_exc())
        row['seconds']=round(time.monotonic()-t,3)
        row['evidence']=[str(p.relative_to(self.out)) for p in sorted(set(self.out.rglob('*'))-before) if p.is_file()]
        if not row['evidence']:
            row['evidence']=[id+'.json'];self.save(id+'.json',row)
        self.rows.append(row);self.save('summary.json',self.rows)
        print(f"{id:5} {row['status']:13} {str(row['actual'])[:150]}",flush=True)
        with (self.out/'summary.csv').open('w',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=list(row))
            writer.writeheader()
            for r in self.rows:writer.writerow({**r,'actual':json.dumps(r['actual'],ensure_ascii=False),'evidence':';'.join(r['evidence'])})
    def bridge_run(self,path,rate=25000000,uart=115200,can=250000):
        p=self.run([self.bridge,path,rate,uart,can],timeout=6)
        assert p.returncode==0,p.stderr
        return json.loads(p.stdout)
    def samples(self,id,data,rate):
        raw=self.out/(id+'.raw');raw.write_bytes(data)
        path=self.out/(id+'.sr');write_sr(path,data,rate)
        return raw,path

class Blocked(Exception):
    def __init__(self,message,status='BLOCKED'):
        super().__init__(message);self.status=status


def require_run(p):
    assert p.returncode==0,p.stderr[-1500:]
    return p.stdout
