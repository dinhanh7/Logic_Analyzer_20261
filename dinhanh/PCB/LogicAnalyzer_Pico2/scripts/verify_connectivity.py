#!/usr/bin/env python3
"""Check exported electrical topology independently of the drawing generator."""
import json,xml.etree.ElementTree as ET
from pathlib import Path
HERE=Path(__file__).resolve().parent.parent
root=ET.parse(HERE/'review/netlist.xml');nets={};by_pin={}
for net in root.findall('.//nets/net'):
 name=net.attrib['name'].lstrip('/');nodes={(n.attrib['ref'],n.attrib['pin']) for n in net.findall('node')};nets[name]=nodes
 for node in nodes:by_pin[node]=name
checks=[]
def check(ok,desc):
 if not ok:raise AssertionError(desc)
 checks.append(desc)
pico_capture=[1,2,4,5,6,7,9,10]
for i,pin in enumerate(pico_capture):
 r=lambda n:(f'R{5*i+n}', '1')
 check(by_pin[(f'U{i+1}','4')]==by_pin[(f'R{5*i+5}','1')],f'CH{i}: buffer Y reaches 33R input')
 check(by_pin[(f'R{5*i+5}','2')]==by_pin[('U9',str(pin))],f'CH{i}: only protected output reaches correct capture GPIO')
 check(nets[f'Z_{i}']=={(f'D{2*i+1}','2'),(f'D{2*i+2}','1')}|({('TP15','1')} if i==0 else set()),f'CH{i}: Zener cathode node private, never supply')
 check(by_pin[(f'D{2*i+1}','1')]=='GND' and by_pin[(f'D{2*i+2}','2')]=='GND',f'CH{i}: clamp current returns to GND')
 check(by_pin[('J1',str(2*i+1))]==by_pin[(f'R{5*i+1}','1')],f'CH{i}: probe pin odd is input')
 check(by_pin[('J1',str(2*i+2))]=='GND',f'CH{i}: probe pin even is ground')
 check(by_pin[(f'R{5*i+1}','2')]==by_pin[(f'R{5*i+2}','1')],f'CH{i}: two series resistors present')
 check(by_pin[(f'R{5*i+2}','2')]==by_pin[(f'D{2*i+1}','3')]==by_pin[(f'R{5*i+3}','1')],f'CH{i}: input clamp precedes 330R and buffer')
for pin,name in {11:'BTN_UP',12:'BTN_DOWN',16:'BTN_OK',29:'BTN_BACK',14:'TFT_SCK',15:'TFT_MOSI',17:'TFT_CS',19:'TFT_DC',20:'TFT_RST',21:'TFT_BL_CTRL',31:'LED_STATUS',32:'CAL_OUT'}.items():check(by_pin['U9',str(pin)]==name,f'Layout A: Pico pin {pin} = {name}')
for pin in (22,24,25,26,27,34):check(by_pin['U9',str(pin)].startswith('unconnected-'),f'Unused Pico pin {pin} remains unconnected')
check(all(r not in ('J1',) and not (r=='U9' and int(n) in pico_capture) for r,n in nets['+3V3']),'No raw input/capture GPIO tied to supply')
check(not any(r=='U9' and int(n) in pico_capture for k,v in nets.items() if k.startswith(('IN','MID_','CLAMP_','A_','Z_')) for r,n in v),'No high-energy/clamp node bypasses the buffer into Pico')
result={'status':'PASS','checks_passed':len(checks),'checks':checks}
(HERE/'review/connectivity_check.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'PASS: {len(checks)} topology / pin-mapping checks.')
