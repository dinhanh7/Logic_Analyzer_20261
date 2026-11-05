#!/usr/bin/env python3
"""Rebuild the native KiCad schematic using the installed KiCad symbol library.

Requires sexpdata (pip install sexpdata). The generated project itself has no
Python dependency; its symbol library is included beside the schematic.
"""
import copy
import csv
import json
import math
import os
from pathlib import Path
import sys
import uuid

try:
    import sexpdata as sx
except ImportError:
    sys.path.insert(0, '/tmp/la_kicad_python')
    import sexpdata as sx

HERE = Path(__file__).resolve().parent
LIB = Path(os.environ.get('LA_KICAD_SYMBOL_DIR',
    '/home/abuntu/Applications/kicad-10.0.8/AppDir/share/kicad/symbols'))
S = sx.Symbol
def uid(): return str(uuid.uuid4())
def q(v): return json.dumps(str(v), ensure_ascii=False)
def mm(v): return round(v * 2.54, 6)
def xy(x, y): return f'{mm(x)} {mm(y)}'
def effects(size=1.1, justify=''):
    return f'(effects (font (size {size} {size})) {justify})'

sources = {'R': ('Device', 'R'), 'C': ('Device', 'C'),
    'D_Zener': ('Device', 'D_Zener'), 'BAV99': ('Diode', 'BAV99'),
    '74LVC1G17': ('74xGxx', '74LVC1G17'),
    'Conn_01x10': ('Connector_Generic', 'Conn_01x10'),
    'PWR_FLAG': ('power', 'PWR_FLAG')}
definitions = {}
for name, (lib, symbol) in sources.items():
    path = LIB / f'{lib}.kicad_symdir' / f'{symbol}.kicad_sym'
    if not path.exists():
        path = LIB / f'{lib}.kicad_sym'
    data = sx.load(path.open())
    definitions[name] = next(v for v in data if isinstance(v, list)
        and str(v[0]) == 'symbol' and v[1] == symbol)

library = [S('kicad_symbol_lib'), [S('version'), 20251024],
    [S('generator'), 'kicad_symbol_editor'], [S('generator_version'), '10.0']]
library += list(definitions.values())
(HERE / 'LAProtection.kicad_sym').write_text(sx.dumps(library) + '\n')
(HERE / 'sym-lib-table').write_text('''(sym_lib_table
  (version 7)
  (lib (name "LAProtection")(type "KiCad")(uri "${KIPRJMOD}/LAProtection.kicad_sym")(options "")(descr "Included protection schematic symbols"))
)\n''')

root_id = uid()
items = []
parts = []
pins = {}
def wire(x1, y1, x2, y2):
    if (x1, y1) != (x2, y2):
        items.append(f'(wire (pts (xy {xy(x1,y1)}) (xy {xy(x2,y2)})) '
            f'(stroke (width 0) (type default)) (uuid "{uid()}"))')
def label(name, x, y):
    items.append(f'(label {q(name)} (at {xy(x,y)} 0) '
        f'{effects(1.1, "(justify left bottom)")} (uuid "{uid()}"))')
def dot(x, y):
    items.append(f'(junction (at {xy(x,y)}) (diameter 0) (color 0 0 0 0) (uuid "{uid()}"))')
def text(value, x, y, size=1.3):
    items.append(f'(text {q(value)} (at {xy(x,y)} 0) '
        f'{effects(size, "(justify left top)")} (uuid "{uid()}"))')
def box(x1,y1,x2,y2):
    items.append(f'(polyline (pts (xy {xy(x1,y1)}) (xy {xy(x2,y1)}) '
        f'(xy {xy(x2,y2)}) (xy {xy(x1,y2)}) (xy {xy(x1,y1)})) '
        f'(stroke (width 0.254) (type default)) (fill (type none)) (uuid "{uid()}"))')
def symbol(name, ref, value, x,y, angle=0, footprint='', datasheet='',
           ref_at=None, val_at=None, note=''):
    ident = uid()
    px, py = ref_at or (x, y-3)
    vx, vy = val_at or (x, y-1.7)
    prop_angle = 90 if angle in (90,270) else 0
    props = f'(property "Reference" {q(ref)} (at {xy(px,py)} {prop_angle}) {effects()})'
    props += f'(property "Value" {q(value)} (at {xy(vx,vy)} {prop_angle}) {effects()})'
    for key, val in [('Footprint',footprint),('Datasheet',datasheet),('Design_note',note)]:
        props += f'(property {q(key)} {q(val)} (at {xy(x,y)} 0) (effects (font (size 1 1)) (hide yes)))'
    items.append(f'(symbol (lib_id "LAProtection:{name}") (at {xy(x,y)} {angle}) '
        f'(unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) '
        f'(dnp no) (uuid "{ident}") {props} '
        f'(instances (project "input_protection" (path "/{root_id}" '
        f'(reference {q(ref)}) (unit 1)))))')
    theta = math.radians(angle)
    loc = {}
    for section in definitions[name]:
        if isinstance(section, list) and str(section[0]) == 'symbol':
            for p in section:
                if isinstance(p,list) and str(p[0]) == 'pin':
                    a = next(v for v in p if isinstance(v,list) and str(v[0])=='at')
                    n = next(v for v in p if isinstance(v,list) and str(v[0])=='number')[1]
                    loc[str(n)] = (round(x+(a[1]*math.cos(theta)-a[2]*math.sin(theta))/2.54,6),
                        round(y-(a[1]*math.sin(theta)+a[2]*math.cos(theta))/2.54,6))
    pins[ref]=loc
    if not ref.startswith('#'):
        parts.append({'Reference':ref,'Value':value,'Footprint':footprint,'Datasheet':datasheet,'Note':note})
    return loc

RFP='Resistor_SMD:R_0603_1608Metric'
PRFP='Resistor_THT:R_Axial_DIN0411_L9.9mm_D3.6mm_P15.24mm_Horizontal'
CFP='Capacitor_SMD:C_0603_1608Metric'
RDS='https://www.vishay.com/docs/28729/pr010203.pdf'
ZDS='https://www.vishay.com/docs/85607/bzx85.pdf'
DDS='https://assets.nexperia.com/documents/data-sheet/BAV99.pdf'
UDS='https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf'
text('LOGIC ANALYZER / 8-CHANNEL INPUT PROTECTION',8,5,2.8)
text('Topic 1 | RP2350 / Pico 2 | 3.3 V supply | Rev A - engineering prototype',8,8,1.5)

# Connectors: pin numbering is visible and deliberately independent of Pico GPIO assignment.
for ref, value, x, nets in [
    ('J1','DUT INPUTS',35,[f'CH{i}_IN' for i in range(8)]+['GND','GND']),
    ('J2','PICO INTERFACE',80,[f'CH{i}_OUT' for i in range(8)]+['+3V3','GND'])]:
    pp=symbol('Conn_01x10',ref,value,x,19,footprint=
        'Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical',
        ref_at=(x+7,16),val_at=(x+7,18))
    for n, net in enumerate(nets,1):
        a,b=pp[str(n)]; wire(a-7,b,a,b); label(net,a-7,b)

symbol('C','C9','4u7 / 10V',101,20,footprint=CFP)
wire(101,18.5,101,16); label('+3V3',101,16)
wire(101,21.5,101,24); label('GND',101,24)
for i,(net,x) in enumerate([('+3V3',113),('GND',125)]):
    symbol('PWR_FLAG',f'#FLG0{i+1:02}', 'PWR_FLAG',x,22,ref_at=(x,19),val_at=(x,20))
    wire(x,22,x,24); label(net,x,24)

text('DESIGN CONDITIONS',141,13,1.8)
text('Target: +12 V DC, +200 V single rectangular pulse <= 1 ms.\n'
     'Common DUT/Pico ground; NOT galvanically isolated.\n'
     'Clamp current returns to GND, never to the +3V3 rail.\n'
     '25 MS/s is the acquisition rate, not input bandwidth.\n'
     'Validate 1 kHz...1 MHz first; characterize higher frequencies.\n'
     'See README.md for calculations, limits and simulation status.',141,16,1.3)

for ch in range(8):
    ox=8+(ch%2)*114; oy=37+(ch//2)*30
    def X(v): return ox+v
    def Y(v): return oy+v
    box(X(0),Y(-7),X(107),Y(21))
    text(f'CH{ch} / PULSE LIMITER -> GND CLAMP -> SCHMITT BUFFER',X(2),Y(-6),1.6)
    r=lambda k:f'R{ch*5+k}'
    d=lambda k:f'D{ch*2+k}'
    u=f'U{ch+1}'; c=f'C{ch+1}'
    for index,pos in [(1,12),(2,22)]:
        symbol('R',r(index),'1k / PR02',X(pos),Y(0),90,PRFP,RDS,
            ref_at=(X(pos),Y(-3.5)),val_at=(X(pos),Y(-2)),
            note='Vishay PR02 Cu-lead, 2W, 1%; pulse load required; do not replace with ordinary 0603')
    wire(X(3),Y(0),X(10.5),Y(0)); label(f'CH{ch}_IN',X(3),Y(0))
    wire(X(13.5),Y(0),X(20.5),Y(0))
    wire(X(23.5),Y(0),X(32),Y(0))
    wire(X(32),Y(0),X(45.5),Y(0)); dot(X(32),Y(0))
    label(f'CLAMP_{ch}',X(33),Y(0))
    symbol('R',r(3),'330R',X(47),Y(0),90,RFP,
        ref_at=(X(47),Y(-3.5)),val_at=(X(47),Y(-2)),
        note='Limits negative input clamp current; also adds RC delay')
    wire(X(48.5),Y(0),X(55),Y(0)); wire(X(55),Y(0),X(65),Y(0)); dot(X(55),Y(0))
    symbol('R',r(4),'47k',X(55),Y(10),0,RFP,
        ref_at=(X(58),Y(9)),val_at=(X(58),Y(11)))
    wire(X(55),Y(0),X(55),Y(8.5))
    wire(X(55),Y(11.5),X(55),Y(15)); label('GND',X(55),Y(15))
    label(f'A_{ch}',X(59),Y(0))

    # BAV99 pin 3 is the midpoint, pin 1 the GND-side anode,
    # pin 2 the isolated Zener-side cathode. Physical SOT-23 mapping preserved.
    symbol('BAV99',d(1),'BAV99',X(32),Y(10),180,
        'Package_TO_SOT_SMD:SOT-23',DDS,
        ref_at=(X(32),Y(12.5)),val_at=(X(32),Y(14)),
        note='1=GND, 2=Zener cathode rail, 3=CLAMP signal; low-capacitance steering pair')
    wire(X(32),Y(0),X(32),Y(8))
    wire(X(35),Y(10),X(41),Y(10)); wire(X(41),Y(10),X(41),Y(15)); label('GND',X(41),Y(15))
    symbol('D_Zener',d(2),'BZX85C3V3',X(17),Y(10),180,
        'Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal',ZDS,
        ref_at=(X(17),Y(12.5)),val_at=(X(17),Y(14)),
        note='3.3V / 1.3W Zener on isolated clamp node; never connect this node to supply +3V3')
    wire(X(18.5),Y(10),X(29),Y(10)); label(f'Z_{ch}',X(22),Y(10))
    wire(X(15.5),Y(10),X(9),Y(10)); wire(X(9),Y(10),X(9),Y(15)); label('GND',X(9),Y(15))

    symbol('74LVC1G17',u,'SN74LVC1G17DBVR',X(70),Y(0),0,
        'Package_TO_SOT_SMD:SOT-23-5',UDS,
        ref_at=(X(73),Y(-4.5)),val_at=(X(72),Y(6)),
        note='TI DBV pinout: 1=NC,2=A,3=GND,4=Y,5=VCC; 5.5V input tolerant and Ioff')
    items.append(f'(no_connect (at {xy(X(67),Y(-2))}) (uuid "{uid()}"))')
    wire(X(68),Y(-4),X(68),Y(-5)); label('+3V3',X(68),Y(-5))
    wire(X(68),Y(4),X(68),Y(9)); label('GND',X(68),Y(9))
    wire(X(74),Y(0),X(83.5),Y(0))
    symbol('R',r(5),'33R',X(85),Y(0),90,RFP,
        ref_at=(X(85),Y(-3.5)),val_at=(X(85),Y(-2)),
        note='Output source termination; place near buffer Y pin')
    wire(X(86.5),Y(0),X(90),Y(0)); label(f'CH{ch}_OUT',X(90),Y(0))
    symbol('C',c,'100n / 10V',X(98),Y(10),0,CFP,
        ref_at=(X(102),Y(9)),val_at=(X(102),Y(11)),
        note='Place immediately beside buffer pins 5 and 3')
    wire(X(98),Y(8.5),X(98),Y(6)); label('+3V3',X(98),Y(6))
    wire(X(98),Y(11.5),X(98),Y(15)); label('GND',X(98),Y(15))
    text('R1/R2 family: pulse-rated PR02, 2 W, 1%; 2k total per channel.',X(3),Y(18),1.05)

text('LAYOUT: keep BAV99/Zener return short; separate surge return from Pico ground path. Keep all eight input paths matched.',8,150,1.3)
text('NO direct connection from CLAMP/A nodes to RP2350. U1...U8 outputs only. Do not fit SMBJ12CA across the signal path.',8,152,1.3)
text('Prototype drawing: ERC checks connectivity only. Component models, parasitics and hardware tests determine the actual protection envelope.',8,154,1.3)

cache=[]
for name,definition in definitions.items():
    obj=copy.deepcopy(definition); obj[1]=f'LAProtection:{name}'; cache.append(sx.dumps(obj))
doc=f'''(kicad_sch (version 20250114) (generator "eeschema") (generator_version "10.0")
 (uuid "{root_id}") (paper "A2")
 (title_block (title "8-channel Logic Analyzer Input Protection") (date "2026-11-05")
 (rev "A") (company "Embedded Systems - Topic 1")
 (comment 1 "Design target: +12V DC / +200V single pulse 1ms; see README for verification scope"))
 (lib_symbols {' '.join(cache)})
 {' '.join(items)}
 (sheet_instances (path "/" (page "1"))) (embedded_fonts no))\n'''
(HERE/'input_protection.kicad_sch').write_text(doc)
(HERE/'input_protection.kicad_pro').write_text(json.dumps({
    'meta':{'filename':'input_protection.kicad_pro','version':1},
    'text_variables':{}, 'schematic':{'meta':{'version':1}}},indent=2)+'\n')
with (HERE/'BOM.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(parts[0])); writer.writeheader(); writer.writerows(parts)
print(f'Created native schematic: {len(parts)} components, 8 channels')
