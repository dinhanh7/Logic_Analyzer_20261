#!/usr/bin/env python3
"""Fetch exactly the reviewed upstream commits; preserve dirty checkouts."""
import json
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
for name,item in json.loads((root/'sources.lock.json').read_text()).items():
    dest=root/'third_party'/name
    if not (dest/'.git').exists():
        dest.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['git','init',str(dest)],check=True)
        subprocess.run(['git','remote','add','origin',item['url']],cwd=dest,check=True)
        subprocess.run(['git','fetch','--depth','1','origin',item['commit']],cwd=dest,check=True)
        subprocess.run(['git','checkout','--detach','FETCH_HEAD'],cwd=dest,check=True)
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=dest,text=True).strip()
    if actual!=item['commit']: raise SystemExit(f'{dest}: unexpected HEAD {actual}; expected {item["commit"]}')
    if name=='pico-sdk': subprocess.run(['git','submodule','update','--init','lib/tinyusb'],cwd=dest,check=True)
