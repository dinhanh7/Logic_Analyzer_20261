#!/usr/bin/env python3
"""Create the review schematic and portable libraries. Requires sexpdata.
KiCad 10 library root may be overridden with KICAD_DATA_DIR.
"""
import copy,csv,json,math,os,sys,uuid,shutil
from pathlib import Path
try: import sexpdata as sx
except ImportError:
 sys.path.insert(0,'/tmp/la_kicad_python'); import sexpdata as sx
HERE=Path(__file__).resolve().parent.parent
DATA=Path(os.environ.get('KICAD_DATA_DIR','/home/abuntu/Applications/kicad-10.0.8/AppDir/share/kicad'))
S=sx.Symbol
PROJECT='LogicAnalyzer_Pico2'
def uid():return str(uuid.uuid4())
def q(v):return json.dumps(str(v),ensure_ascii=False)
def mm(v):return round(v*2.54,6)
def xy(x,y):return f'{mm(x)} {mm(y)}'
def effects(size=1.1,justify=''):return f'(effects (font (size {size} {size})) {justify})'
sources={'R':('Device','R'),'C':('Device','C'),'LED':('Device','LED'),'D_Zener':('Device','D_Zener'),'BAV99':('Diode','BAV99'),'74LVC1G17':('74xGxx','74LVC1G17'),'Probe_2x08':('Connector_Generic','Conn_02x08_Odd_Even'),'Conn_01x08':('Connector_Generic','Conn_01x08'),'Conn_01x02':('Connector_Generic','Conn_01x02'),'TestPoint':('Connector','TestPoint'),'SW_Push':('Switch','SW_Push'),'Q_NPN_BEC':('Transistor_BJT','Q_NPN_BEC'),'Q_PNP_BEC':('Transistor_BJT','Q_PNP_BEC'),'MountingHole':('Mechanical','MountingHole')}
definitions={}
for alias,(lib,name) in sources.items():
 p=DATA/'symbols'/f'{lib}.kicad_symdir'/f'{name}.kicad_sym'
 if not p.exists():p=DATA/'symbols'/f'{lib}.kicad_sym'
 d=sx.load(p.open());v=copy.deepcopy(next(v for v in d if isinstance(v,list) and str(v[0])=='symbol' and v[1]==name))
 v[1]=alias
 for sec in v:
  if isinstance(sec,list) and str(sec[0])=='symbol':sec[1]=sec[1].replace(name,alias)
 definitions[alias]=v
# Correct misleading legacy pin names; diode graphics and package numbers are unchanged.
for sec in definitions['BAV99']:
 if isinstance(sec,list) and str(sec[0])=='symbol':
  for p in sec:
   if isinstance(p,list) and str(p[0])=='pin':
    n=next(v[1] for v in p if isinstance(v,list) and str(v[0])=='number')
    next(v for v in p if isinstance(v,list) and str(v[0])=='name')[1]={'1':'A1','2':'K2','3':'K1_A2'}[n]
# Socketed module symbol, physical 40-pin ordering, no hidden stacked pins.
left=['GP0','GP1','GND','GP2','GP3','GP4','GP5','GND','GP6','GP7','GP8','GP9','GND','GP10','GP11','GP12','GP13','GND','GP14','GP15']
right=['VBUS','VSYS','GND','3V3_EN','3V3_OUT','ADC_VREF','GP28','AGND','GP27','GP26','RUN','GP22','GND','GP21','GP20','GP19','GP18','GND','GP17','GP16']
v=f'''(symbol "Pico2_SOCKET" (pin_names (offset 1)) (exclude_from_sim no) (in_bom yes) (on_board yes)
(property "Reference" "U" (at 0 30.48 0) {effects()}) (property "Value" "Pico 2 socketed" (at 0 27.94 0) {effects()})
(symbol "Pico2_SOCKET_0_1" (rectangle (start -17.78 26.67) (end 17.78 -26.67) (stroke (width .254) (type default)) (fill (type background))))
(symbol "Pico2_SOCKET_1_1" '''
for side,names in [(-1,left),(1,right)]:
 for i,name in enumerate(names):
  n=i+1 if side==-1 else 40-i
  typ='power_out' if n in (3,36) else ('passive' if name in ('GND','AGND','VSYS','VBUS','RUN','ADC_VREF','3V3_EN') else 'bidirectional')
  v+=f'(pin {typ} line (at {side*22.86} {24.13-i*2.54} {0 if side==-1 else 180}) (length 5.08) (name "{name}" {effects()}) (number "{n}" {effects()}))'
v+='))';definitions['Pico2_SOCKET']=sx.loads(v)
library=[S('kicad_symbol_lib'),[S('version'),20251024],[S('generator'),'kicad_symbol_editor'],[S('generator_version'),'10.0']]+list(definitions.values())
(HERE/'LogicAnalyzer.kicad_sym').write_text(sx.dumps(library)+'\n')
(HERE/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "LogicAnalyzer")(type "KiCad")(uri "${KIPRJMOD}/LogicAnalyzer.kicad_sym")(options "")(descr "Project symbols")))\n')
(HERE/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "LogicAnalyzer")(type "KiCad")(uri "${KIPRJMOD}/LogicAnalyzer.pretty")(options "")(descr "Included project footprints")))\n')
# Copy only required footprints and STEP models. Names are unique in the local library.
fp_sources={
 'R0805':'Resistor_SMD:R_0805_2012Metric','C0805':'Capacitor_SMD:C_0805_2012Metric',
 'C1206':'Capacitor_SMD:C_1206_3216Metric','SOT23':'Package_TO_SOT_SMD:SOT-23',
 'SOT23_5':'Package_TO_SOT_SMD:SOT-23-5','Zener_DO41':'Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal',
 'Probe_2x08':'Connector_PinHeader_2.54mm:PinHeader_2x08_P2.54mm_Vertical',
 'TFT_1x08':'Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical',
 'CAL_1x02':'Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical',
 'SW_6mm':'Button_Switch_THT:SW_PUSH_6mm','LED0805':'LED_SMD:LED_0805_2012Metric',
 'TP_1mm':'TestPoint:TestPoint_Pad_D1.0mm','Hole_M3':'MountingHole:MountingHole_3.2mm_M3',
 'Pico2_Sockets':'Module:RaspberryPi_Pico_Common_THT'}
for alias,source in fp_sources.items():
 lib,name=source.split(':');d=sx.load((DATA/'footprints'/f'{lib}.pretty'/f'{name}.kicad_mod').open());d[1]=alias
 if alias=='Pico2_Sockets':
  # Pico 2 has no antenna; keep USB cable mechanical outline only.
  d=[t for t in d if not (isinstance(t,list) and str(t[0])=='zone' and any(isinstance(z,list) and str(z[0])=='name' and z[1]=='Antenna Copper Keep Out' for z in t))]
 for t in d:
  if isinstance(t,list) and str(t[0])=='model':
   rel=t[1].split('}/',1)[-1];src=DATA/'3dmodels'/rel
   if src.exists():shutil.copyfile(src,HERE/'models'/src.name);t[1]='${KIPRJMOD}/models/'+src.name
 (HERE/'LogicAnalyzer.pretty'/f'{alias}.kicad_mod').write_text(sx.dumps(d)+'\n')
# PR02 Cu-lead: max coated body 10x3.9, lead envelope 12mm, 0.78mm wire. 17.78mm pitch.
fp='''(footprint "PR02_1k_2W_P17.78" (version 20241229) (generator "pcbnew") (layer "F.Cu")
(descr "Vishay PR02 Cu 2W, body 10x3.9 max, lead envelope 12mm, pitch 17.78mm") (attr through_hole)
(property "Reference" "REF**" (at 8.89 -3 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness .15))))
(property "Value" "1k PR02 2W" (at 8.89 3 0) (layer "F.Fab") (effects (font (size 1 1) (thickness .15))))
(fp_rect (start 3.89 -1.95) (end 13.89 1.95) (stroke (width .12) (type default)) (fill none) (layer "F.SilkS"))
(fp_line (start 1.4 0) (end 3.89 0) (stroke (width .12) (type default)) (layer "F.SilkS"))
(fp_line (start 13.89 0) (end 16.38 0) (stroke (width .12) (type default)) (layer "F.SilkS"))
(fp_rect (start -1.65 -2.5) (end 19.43 2.5) (stroke (width .05) (type default)) (fill none) (layer "F.CrtYd"))
(fp_rect (start 3.89 -1.95) (end 13.89 1.95) (stroke (width .1) (type default)) (fill none) (layer "F.Fab"))
(pad "1" thru_hole circle (at 0 0) (size 2.6 2.6) (drill 1.2) (layers "*.Cu" "*.Mask"))
(pad "2" thru_hole circle (at 17.78 0) (size 2.6 2.6) (drill 1.2) (layers "*.Cu" "*.Mask")))'''
(HERE/'LogicAnalyzer.pretty'/'PR02_1k_2W_P17.78.kicad_mod').write_text(fp+'\n')
root_id=uid();items=[];parts=[];pins={};connections={}
def wire(x1,y1,x2,y2):
 if (x1,y1)!=(x2,y2):items.append(f'(wire (pts (xy {xy(x1,y1)}) (xy {xy(x2,y2)})) (stroke (width 0) (type default)) (uuid "{uid()}"))')
def label(net,x,y):items.append(f'(label {q(net)} (at {xy(x,y)} 0) {effects(1.05,"(justify left bottom)")} (uuid "{uid()}"))')
def text(v,x,y,size=1.3):items.append(f'(text {q(v)} (at {xy(x,y)} 0) {effects(size,"(justify left top)")} (uuid "{uid()}"))')
def dot(x,y):items.append(f'(junction (at {xy(x,y)}) (diameter 0) (color 0 0 0 0) (uuid "{uid()}"))')
def box(x1,y1,x2,y2):items.append(f'(polyline (pts (xy {xy(x1,y1)}) (xy {xy(x2,y1)}) (xy {xy(x2,y2)}) (xy {xy(x1,y2)}) (xy {xy(x1,y1)})) (stroke (width .254) (type default)) (fill (type none)) (uuid "{uid()}"))')
def symbol(name,ref,value,x,y,angle=0,footprint='',datasheet='',ref_at=None,val_at=None,note='',bom=True):
 ident=uid();px,py=ref_at or (x,y-3.5);vx,vy=val_at or (x,y-2)
 props=f'(property "Reference" {q(ref)} (at {xy(px,py)} 0) {effects()}) (property "Value" {q(value)} (at {xy(vx,vy)} 0) {effects()})'
 for k,val in [('Footprint','LogicAnalyzer:'+footprint if footprint else ''),('Datasheet',datasheet),('Design_note',note)]:props+=f'(property {q(k)} {q(val)} (at {xy(x,y)} 0) (effects (font (size 1 1)) (hide yes)))'
 items.append(f'(symbol (lib_id "LogicAnalyzer:{name}") (at {xy(x,y)} {angle}) (unit 1) (exclude_from_sim no) (in_bom {"yes" if bom else "no"}) (on_board yes) (dnp no) (uuid "{ident}") {props} (instances (project "{PROJECT}" (path "/{root_id}" (reference {q(ref)}) (unit 1)))))')
 theta=math.radians(angle);loc={}
 for sec in definitions[name]:
  if isinstance(sec,list) and str(sec[0])=='symbol':
   for p in sec:
    if isinstance(p,list) and str(p[0])=='pin':
     a=next(v for v in p if isinstance(v,list) and str(v[0])=='at');n=next(v[1] for v in p if isinstance(v,list) and str(v[0])=='number')
     loc[str(n)]=(round(x+(a[1]*math.cos(theta)-a[2]*math.sin(theta))/2.54,6),round(y-(a[1]*math.sin(theta)+a[2]*math.cos(theta))/2.54,6))
 pins[ref]=loc;connections[ref]={}
 parts.append({'Reference':ref,'Value':value,'Footprint':'LogicAnalyzer:'+footprint,'Datasheet':datasheet,'Note':note,'UUID':ident,'BOM':bom})
 return loc

def connect(ref,n,net,direction='left',length=3):
 n=str(n);a,b=pins[ref][n];dx,dy={'left':(-length,0),'right':(length,0),'up':(0,-length),'down':(0,length)}[direction]
 wire(a,b,a+dx,b+dy);label(net,a+dx,b+dy);connections[ref][n]=net

def nc(ref,n):
 a,b=pins[ref][str(n)];items.append(f'(no_connect (at {xy(a,b)}) (uuid "{uid()}"))');connections[ref][str(n)]=None

def two(name,ref,value,x,y,fp,n1,n2,angle=90,ds='',note=''):
 symbol(name,ref,value,x,y,angle,fp,ds,note=note)
 connect(ref,1,n1,'left' if angle==90 else 'up');connect(ref,2,n2,'right' if angle==90 else 'down')

RDS='https://www.vishay.com/docs/28729/pr010203.pdf';ZDS='https://www.vishay.com/docs/85607/bzx85.pdf';DDS='https://assets.nexperia.com/documents/data-sheet/BAV99.pdf';UDS='https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf'
text('LOGIC ANALYZER - PICO 2 CARRIER / LAYOUT A',8,5,3)
text('Rev A1 | 8 channels | 25 MS/s capture | USB-only | ST7735 SPI1 | engineering review',8,9,1.5)
text('PICO MODULE / FEMALE SOCKETS',8,14,1.8)
symbol('Pico2_SOCKET','U9','Pico 2 + 2x female 1x20',28,32,footprint='Pico2_Sockets',datasheet='https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf',ref_at=(28,18),val_at=(28,20),note='Fit two female 1x20 2.54mm sockets, row spacing 17.78mm; USB and regulator are on the plug-in Pico 2.')
piconets={1:'OUT0',2:'OUT1',4:'OUT2',5:'OUT3',6:'OUT4',7:'OUT5',9:'OUT6',10:'OUT7',11:'BTN_UP',12:'BTN_DOWN',14:'TFT_SCK',15:'TFT_MOSI',16:'BTN_OK',17:'TFT_CS',19:'TFT_DC',20:'TFT_RST',21:'TFT_BL_CTRL',29:'BTN_BACK',30:'RUN',31:'LED_STATUS',32:'CAL_OUT',36:'+3V3',39:'VSYS'}
for n in (3,8,13,18,23,28,33,38):piconets[n]='GND'
for n in range(1,41):
 if n in piconets:connect('U9',n,piconets[n],'left' if n<=20 else 'right',3)
 else:nc('U9',n)
text('USB power/data through Pico only.\nNo external VSYS supply.\nGP17..GP21, GP28 unconnected.\nKeep BOOTSEL / module SWD accessible.',7,48,1.2)
# Probe connector, explicit odd/even convention.
symbol('Probe_2x08','J1','PROBE CH0..7 / GND',68,27,footprint='Probe_2x08',ref_at=(68,19),val_at=(68,21))
for n in range(1,17):connect('J1',n, f'IN{(n-1)//2}' if n%2 else 'GND','left' if n%2 else 'right',4)
text('ODD: CH0..7\nEVEN: GND\nCommon ground; no isolation.',58,39,1.2)
# TFT connector: cable mapping, not a claim that arbitrary TFT modules mate directly.
symbol('Conn_01x08','J2','TFT CABLE / 3V3 ONLY',101,27,footprint='TFT_1x08',ref_at=(101,19),val_at=(101,21),note='Generic labeled cable header. Verify actual TFT pin order before wiring. No MISO.')
for n,net in enumerate(['GND','+3V3','TFT_SCK','TFT_MOSI','TFT_RST','TFT_DC','TFT_CS','TFT_BL'],1):connect('J2',n,net,'left',6)
text('1 GND  2 3V3  3 SCK  4 MOSI\n5 RST  6 DC   7 CS   8 BL\nCable adapter to actual ST7735 module.',87,39,1.2)
# 4 buttons and reset, firmware internal pullups.
for i,(name,net) in enumerate([('UP','BTN_UP'),('DOWN','BTN_DOWN'),('OK','BTN_OK'),('BACK','BTN_BACK'),('RESET','RUN')]):
 x=124+(i%3)*21;y=27+(i//3)*14
 symbol('SW_Push',f'SW{i+1}',name,x,y,footprint='SW_6mm')
 connect(f'SW{i+1}',1,net);connect(f'SW{i+1}',2,'GND','right')
text('Buttons: internal Pico pull-ups + firmware debounce.\nRESET shorts RUN to GND; pull-up already on Pico.',121,48,1.2)
# Power and indicators.
two('C','C9','10u / 10V X7R',186,28,'C1206','+3V3','GND',0,note='Bulk, effective capacitance at 3.3V to be checked.')
two('C','C10','10u / 10V X7R',201,28,'C1206','+3V3','GND',0,note='Near TFT header.')
for idx,(net,title) in enumerate([('+3V3','PWR'),('LED_STATUS','STATUS')]):
 x=182+idx*26
 two('R',f'R{41+idx}','1k',x,42,'R0805',net,f'LED{idx}_A')
 symbol('LED',f'D{17+idx}',title,x+11,42,180,'LED0805')
 connect(f'D{17+idx}',1,'GND','right');connect(f'D{17+idx}',2,f'LED{idx}_A','left')
symbol('Conn_01x02','J3','CAL OUT / GND',226,28,footprint='CAL_1x02')
connect('J3',1,'CAL_OUT');connect('J3',2,'GND')
text('CAL = GP27; connect to input with an external wire.',215,35,1.1)
# Backlight high side, active-high, current limited for unknown module variant.
text('TFT BACKLIGHT - ACTIVE HIGH, DEFAULT 100R LIMIT',8,58,1.6)
symbol('Q_NPN_BEC','Q1','BC817-40',22,67,footprint='SOT23',note='B=1 E=2 C=3; drives high-side PNP only, not backlight current directly.')
connect('Q1',1,'BL_N_BASE');connect('Q1',2,'GND','down');connect('Q1',3,'BL_N_COL','up')
two('R','R43','4k7',10,67,'R0805','TFT_BL_CTRL','BL_N_BASE')
two('R','R44','47k',35,67,'R0805','BL_N_BASE','GND',0)
symbol('Q_PNP_BEC','Q2','BC807-40',64,67,footprint='SOT23',note='B=1 E=2 C=3; high-side backlight power. Module must tolerate 3.3V BL.')
connect('Q2',1,'BL_P_BASE');connect('Q2',2,'+3V3','down');connect('Q2',3,'BL_PWR','up')
two('R','R45','4k7',49,64,'R0805','BL_N_COL','BL_P_BASE')
two('R','R46','47k',78,67,'R0805','BL_P_BASE','+3V3',0)
two('R','R47','100R',91,64,'R0805','BL_PWR','TFT_BL')
two('R','R48','100k',108,67,'R0805','TFT_BL','GND',0)
text('Default BL series 100R limits current <=33mA; can dim some modules.\nChange only after checking module BL circuit/current. GP16 HIGH = ON.\nDo not connect raw backlight LED to GP16.',121,59,1.2)
# Testpoints.
for idx,(ref,net) in enumerate([('TP9','+3V3'),('TP10','VSYS'),('TP11','GND'),('TP12','IN0'),('TP13','CLAMP_0'),('TP14','A_0'),('TP15','Z_0')]):
 x=180+(idx%4)*16;y=61+(idx//4)*10
 symbol('TestPoint',ref,net,x,y,footprint='TP_1mm',ref_at=(x+3,y-2),val_at=(x+3,y))
 connect(ref,1,net,'down',2)
# 8 identical independently clamped channels.
for ch in range(8):
 ox=8+(ch%2)*119;oy=85+(ch//2)*30
 X=lambda t:ox+t;Y=lambda t:oy+t
 box(X(0),Y(-7),X(113),Y(21));text(f'CH{ch}   PULSE LIMITER / CLAMP TO GND / SCHMITT BUFFER',X(2),Y(-6),1.6)
 r=lambda k:f'R{ch*5+k}';d=lambda k:f'D{ch*2+k}';u=f'U{ch+1}'
 for i,pos in [(1,12),(2,24)]:symbol('R',r(i),'1k PR02 2W',X(pos),Y(0),90,'PR02_1k_2W_P17.78',RDS,ref_at=(X(pos),Y(-3.5)),val_at=(X(pos),Y(-2)),note='Cu-lead PR02, 1%, proposed order code PR02000201001FA100; validate pulse curve. Do not replace with ordinary SMD resistor.')
 wire(X(3),Y(0),X(10.5),Y(0));label(f'IN{ch}',X(3),Y(0))
 wire(X(13.5),Y(0),X(22.5),Y(0));label(f'MID_{ch}',X(15),Y(0))
 wire(X(25.5),Y(0),X(34),Y(0));wire(X(34),Y(0),X(47.5),Y(0));dot(X(34),Y(0));label(f'CLAMP_{ch}',X(36),Y(0))
 symbol('R',r(3),'330R',X(49),Y(0),90,'R0805');wire(X(50.5),Y(0),X(57),Y(0));wire(X(57),Y(0),X(67),Y(0));dot(X(57),Y(0));label(f'A_{ch}',X(61),Y(0))
 symbol('R',r(4),'47k',X(57),Y(10),footprint='R0805',ref_at=(X(60),Y(9)),val_at=(X(60),Y(11)))
 wire(X(57),Y(0),X(57),Y(8.5));wire(X(57),Y(11.5),X(57),Y(15));label('GND',X(57),Y(15))
 symbol('BAV99',d(1),'BAV99',X(34),Y(10),180,'SOT23',DDS,ref_at=(X(34),Y(12.5)),val_at=(X(34),Y(14)),note='1=GND (A1), 2=Z (K2), 3=CLAMP (K1/A2). Z is private to this channel, NOT +3V3.')
 wire(X(34),Y(0),X(34),Y(8));wire(X(37),Y(10),X(43),Y(10));wire(X(43),Y(10),X(43),Y(15));label('GND',X(43),Y(15))
 symbol('D_Zener',d(2),'BZX85C3V3',X(18),Y(10),180,'Zener_DO41',ZDS,ref_at=(X(18),Y(12.5)),val_at=(X(18),Y(14)))
 wire(X(19.5),Y(10),X(31),Y(10));label(f'Z_{ch}',X(23),Y(10));wire(X(16.5),Y(10),X(9),Y(10));wire(X(9),Y(10),X(9),Y(15));label('GND',X(9),Y(15))
 symbol('74LVC1G17',u,'SN74LVC1G17DBVR',X(72),Y(0),footprint='SOT23_5',datasheet=UDS,ref_at=(X(75),Y(-4.5)),val_at=(X(74),Y(6)),note='DBV 1=NC 2=A 3=GND 4=Y 5=VCC. Only Y ->33R connects to Pico.')
 nc(u,1);wire(X(70),Y(-4),X(70),Y(-5));label('+3V3',X(70),Y(-5));wire(X(70),Y(4),X(70),Y(10));label('GND',X(70),Y(10))
 wire(X(76),Y(0),X(86.5),Y(0));label(f'Y_{ch}',X(79),Y(0));symbol('R',r(5),'33R',X(88),Y(0),90,'R0805')
 wire(X(89.5),Y(0),X(94),Y(0));label(f'OUT{ch}',X(94),Y(0))
 symbol('C',f'C{ch+1}','100n 10V',X(104),Y(10),footprint='C0805',ref_at=(X(108),Y(9)),val_at=(X(108),Y(11)))
 connect(f'C{ch+1}',1,'+3V3','up',2);connect(f'C{ch+1}',2,'GND','down',2)
 symbol('TestPoint',f'TP{ch+1}',f'OUT{ch}',X(102),Y(0),footprint='TP_1mm',ref_at=(X(107),Y(-3.5)),val_at=(X(107),Y(-2)))
 connect(f'TP{ch+1}',1,f'OUT{ch}','down',3)
 text('Target +12V continuous / +200V single 1ms; validate with exact parts + parasitics.',X(3),Y(18),1.05)
# Mechanical-only components, explicit in schematic for board parity.
for n in range(4):symbol('MountingHole',f'H{n+1}','M3 / 3.2mm',260+n*12,90,footprint='Hole_M3',bom=False)
text('ASSEMBLY / VALIDATION NOTES',255,15,1.8)
text('Two-layer carrier, 160 x 110 mm.\nPico 2 plugs into 2x20 female sockets.\nTFT uses labeled cable connector J2.\nNo unverified direct-mating TFT footprint.\n\nAll eight Z nodes are independent.\nNever connect Z/CLAMP to +3V3.\nGP0..7 connect only to buffer Y via 33R.\n\n3V3 load target below 300mA; measure\nPico + TFT + BL before release.\n\n25 MS/s is sampling rate.\nInput signal target 1kHz..1MHz;\nvalidate bandwidth and timing on board.\n\nEngineering review, not production release.\nHigh-voltage pulse rating is a design\ntarget; ERC/DRC cannot validate it.',255,20,1.3)
cache=[]
for name,v in definitions.items():
 d=copy.deepcopy(v);d[1]='LogicAnalyzer:'+name;cache.append(sx.dumps(d))
doc=f'''(kicad_sch (version 20250114) (generator "eeschema") (generator_version "10.0") (uuid "{root_id}") (paper "A1")
(title_block (title "Pico 2 Logic Analyzer Carrier - Layout A") (date "2026-11-11") (rev "A1") (company "Logic Analyzer 2026.1") (comment 1 "Engineering review - see README for module mapping and validation limits"))
(lib_symbols {' '.join(cache)}) {' '.join(items)} (sheet_instances (path "/" (page "1"))) (embedded_fonts no))\n'''
(HERE/f'{PROJECT}.kicad_sch').write_text(doc)
# Retain project settings across reruns.
if not (HERE/f'{PROJECT}.kicad_pro').exists():(HERE/f'{PROJECT}.kicad_pro').write_text(json.dumps({'meta':{'filename':f'{PROJECT}.kicad_pro','version':1},'text_variables':{},'schematic':{'meta':{'version':1}}},indent=2)+'\n')
(HERE/'design_manifest.json').write_text(json.dumps({'root_uuid':root_id,'parts':parts,'pico_nets':piconets},indent=2,ensure_ascii=False)+'\n')
with (HERE/'BOM.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=['Reference','Value','Footprint','Datasheet','Note']);w.writeheader();w.writerows({k:p[k] for k in w.fieldnames} for p in parts if p['BOM'])
print(f'Generated {len(parts)} components, native schematic + local symbol/footprint/3D libraries.')
