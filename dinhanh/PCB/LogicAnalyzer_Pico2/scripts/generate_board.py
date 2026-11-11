#!/usr/bin/env python3
"""Create placed board + local critical routes from KiCad-exported XML netlist.
Run with the Python bundled with KiCad 10 (pcbnew required).
Then autoroute review/unrouted.dsn locally and import the .ses with finish_board.py.
"""
import pcbnew as p,json,xml.etree.ElementTree as ET
from pathlib import Path
HERE=Path(__file__).resolve().parent.parent
NAME='LogicAnalyzer_Pico2';OUT=HERE/f'{NAME}.kicad_pcb'
manifest=json.loads((HERE/'design_manifest.json').read_text());parts={v['Reference']:v for v in manifest['parts']}
xml=ET.parse(HERE/'review/netlist.xml');board=p.BOARD();board.SetCopperLayerCount(2)
board.SetFileName(str(OUT));ds=board.GetDesignSettings();ds.SetBoardThickness(p.FromMM(1.6))
ds.m_MinClearance=p.FromMM(.2);ds.m_TrackMinWidth=p.FromMM(.2);ds.m_ViasMinSize=p.FromMM(.6);ds.m_MinThroughDrill=p.FromMM(.3);ds.m_HoleToHoleMin=p.FromMM(.25);ds.m_CopperEdgeClearance=p.FromMM(.5)
classes=[('Default',.22,.25,.6,.3),('Power',.22,.5,.8,.4),('PulseInput',.75,.4,.8,.4)]
ns=ds.m_NetSettings
for name,clear,width,via,drill in classes:
 c=p.NETCLASS(name);c.SetClearance(p.FromMM(clear));c.SetTrackWidth(p.FromMM(width));c.SetViaDiameter(p.FromMM(via));c.SetViaDrill(p.FromMM(drill))
 if name=='Default':ns.SetDefaultNetclass(c)
 else:ns.SetNetclass(name,c)
netobjs={};pinnets={}
assign={}
for n in xml.findall('.//nets/net'):
 name=n.attrib['name'];net=p.NETINFO_ITEM(board,name);board.Add(net);netobjs[name]=net
 cls='Power' if name in ('/+3V3','/GND') else ('PulseInput' if name.startswith(('/IN','/MID_')) else 'Default')
 if cls!='Default':ns.SetNetclassPatternAssignment(name,cls);assign[name]=[cls]
 for node in n.findall('node'):pinnets[node.attrib['ref'],node.attrib['pin']]=net
ns.RecomputeEffectiveNetclasses()
proj=json.loads((HERE/f'{NAME}.kicad_pro').read_text())
proj['net_settings']={'classes':[{'name':n,'clearance':c,'track_width':w,'via_diameter':v,'via_drill':d,'priority':2147483647 if n=='Default' else i} for i,(n,c,w,v,d) in enumerate(classes)],'meta':{'version':4},'net_colors':None,'netclass_assignments':assign,'netclass_patterns':[]}
proj['board']={'design_settings':{'meta':{'version':2},'drc_exclusions':[],'rules':{'min_clearance':.2,'min_track_width':.2,'min_via_diameter':.6,'min_through_hole_diameter':.3,'min_via_annular_width':.15,'min_hole_to_hole':.25,'min_hole_clearance':.25,'min_copper_edge_clearance':.5,'min_silk_clearance':.1,'min_text_height':.8,'min_text_thickness':.12,'solder_mask_clearance':0.0,'solder_mask_min_width':.05},'track_widths':[0,.25,.4,.5,.8],'via_dimensions':[{'diameter':.6,'drill':.3},{'diameter':.8,'drill':.4}]}}
(HERE/f'{NAME}.kicad_pro').write_text(json.dumps(proj,indent=2)+'\n')
(HERE/f'{NAME}.kicad_dru').write_text('''(version 1)
(rule "Pulse input copper spacing - prototype target"
 (condition "A.NetName == '/IN*' || A.NetName == '/MID_*'")
 (constraint clearance (min 0.75mm)))
''')
fp={}
def V(x,y):return p.VECTOR2I(p.FromMM(x),p.FromMM(y))
def put(ref,x,y,angle=0):
 part=parts[ref];f=p.FootprintLoad(str(HERE/'LogicAnalyzer.pretty'),part['Footprint'].split(':')[1]);assert f,part['Footprint']
 board.Add(f);f.SetReference(ref);f.SetValue(part['Value']);f.SetFPID(p.LIB_ID(*part['Footprint'].split(':')));f.SetOrientationDegrees(angle);f.SetPosition(V(x,y))
 path=p.KIID_PATH();path.push_back(p.KIID(manifest['root_uuid']));path.push_back(p.KIID(part['UUID']));f.SetPath(path);f.SetSheetfile(f'{NAME}.kicad_sch');f.SetSheetname('')
 f.SetAttributes((f.GetAttributes() & ~p.FP_EXCLUDE_FROM_BOM) if part['BOM'] else (f.GetAttributes() | p.FP_EXCLUDE_FROM_BOM))
 f.SetField('Design_note',part['Note'])
 f.SetField('Datasheet',part['Datasheet'])
 f.GetField('Design_note').SetVisible(False)
 f.GetField('Datasheet').SetVisible(False)
 for pad in f.Pads():
  net=pinnets.get((ref,pad.GetNumber()))
  if net:pad.SetNet(net)
 for field in [f.Reference(),f.Value()]:field.SetTextSize(V(.9,.9));field.SetTextThickness(p.FromMM(.13))
 f.Value().SetVisible(False);f.Reference().SetVisible(True)
 # Explicit horizontal ref position; later refine any DRC text clashes.
 f.Reference().SetTextAngle(p.EDA_ANGLE(0,p.DEGREES_T));f.Reference().SetPosition(V(x,y-2.2))
 fp[ref]=f;return f

def pad(ref,n):return next(t for t in fp[ref].Pads() if t.GetNumber()==str(n))
def pos(ref,n):
 v=pad(ref,n).GetPosition();return (p.ToMM(v.x),p.ToMM(v.y))
def trace(net,points,layer=p.F_Cu,width=.25):
 if not isinstance(net,p.NETINFO_ITEM):net=netobjs['/'+net if not net.startswith('/') else net]
 for a,b in zip(points,points[1:]):
  if a==b:continue
  t=p.PCB_TRACK(board);t.SetStart(V(*a));t.SetEnd(V(*b));t.SetWidth(p.FromMM(width));t.SetLayer(layer);t.SetNet(net);board.Add(t)
def join(r1,n1,r2,n2,layer=p.F_Cu,width=.25):
 a=pad(r1,n1);b=pad(r2,n2);assert a.GetNetCode()==b.GetNetCode(),(r1,n1,r2,n2)
 trace(a.GetNet(),[pos(r1,n1),pos(r2,n2)],layer,width)
def via(net,x,y,size=.6,drill=.3):
 v=p.PCB_VIA(board);v.SetPosition(V(x,y));v.SetWidth(p.FromMM(size));v.SetDrill(p.FromMM(drill));v.SetViaType(p.VIATYPE_THROUGH);v.SetLayerPair(p.F_Cu,p.B_Cu);v.SetNet(netobjs['/'+net]);board.Add(v)
# Placements: high-energy inputs at left; isolated clamp nodes; digital at right.
put('J1',9,44)
put('U9',104,3.2)
for ch in range(8):
 y=14+11.2*ch
 put(f'R{ch*5+1}',23,y);put(f'R{ch*5+2}',46,y)
 put(f'D{ch*2+1}',71,y)
 put(f'D{ch*2+2}',68,y+5.3,180) # cathode pin1 at x68, anode pin2 at x57.84
 put(f'R{ch*5+3}',76,y)
 put(f'R{ch*5+4}',78,y+3.4,270)
 put(f'U{ch+1}',82,y)
 put(f'R{ch*5+5}',86.5,y+.95)
 put(f'C{ch+1}',85.7,y-3.4)
 put(f'TP{ch+1}',92,y+.95)
 # PR02 identities centered above bodies.
 for k in (1,2):fp[f'R{ch*5+k}'].Reference().SetPosition(V(23+(k-1)*23+8.89,y-3.1))
 # No text over adjacent SMD pads.
 for ref,xx,yy in [(f'D{ch*2+1}',70,y-2.8),(f'D{ch*2+2}',62.9,y+8.6),(f'R{ch*5+3}',75.5,y-2.2),(f'R{ch*5+4}',78.0,y+5.8),(f'U{ch+1}',82,y+3.0),(f'R{ch*5+5}',86.5,y+3),(f'C{ch+1}',85.7,y-5.4),(f'TP{ch+1}',94,y+.95)]:fp[ref].Reference().SetPosition(V(xx,yy))
 # Local critical signal paths, short copper before the auto-routed Pico fanout.
 join(f'R{ch*5+1}',2,f'R{ch*5+2}',1,width=.4)
 join(f'R{ch*5+2}',2,f'D{ch*2+1}',3,width=.4)
 join(f'D{ch*2+1}',3,f'R{ch*5+3}',1)
 join(f'R{ch*5+3}',2,f'U{ch+1}',2)
 join(f'U{ch+1}',4,f'R{ch*5+5}',1)
 join(f'R{ch*5+5}',2,f'TP{ch+1}',1)
 # Z private return: cathode of Zener to BAV99 pin2.
 join(f'D{ch*2+1}',2,f'D{ch*2+2}',1)
 # 330R -> pulldown, routed orthogonally away from IC's GND pad.
 a=pos(f'R{ch*5+4}',1);b=pos(f'R{ch*5+3}',2)
 trace(f'A_{ch}',[a,(a[0],y),b])
 # Direct decoupling branch, one via at VCC and one at ground later if router needs.
 a=pos(f'C{ch+1}',1);b=pos(f'U{ch+1}',5)
 trace('+3V3',[a,(a[0],y-1.9),(b[0],y-1.9),b],width=.4)
# Interface area.
put('J2',132,18,90)
put('C9',128,27);put('C10',143,23)
put('J3',152,48)
for ref,x,y in [('SW1',130,70),('SW2',147,70),('SW3',130,87),('SW4',147,87),('SW5',132,42)]:put(ref,x,y)
for ref,x,y in [('D17',131,8),('D18',146,8),('R41',127,8),('R42',142,8),('Q1',135,32),('Q2',146,32),('R43',131,32),('R44',135,36),('R45',140,30),('R46',146,36),('R47',151,28),('R48',155,34)]:put(ref,x,y)
for ref,x,y in [('TP9',128,58),('TP10',127,13),('TP11',149,58),('TP12',19,14),('TP13',68,10),('TP14',78,9),('TP15',72,21)]:put(ref,x,y)
for ref,x,y in [('H1',4,4),('H2',156,4),('H3',4,106),('H4',156,106)]:put(ref,x,y)
# Short LED routes (LED cathode pad1 left; source resistor at left).
# LEDs oriented 180 to have cathode at right, away from source resistor.
for ref in ('D17','D18'):
 f=fp[ref];f.SetOrientationDegrees(180)
join('R41',2,'D17',2);join('R42',2,'D18',2)
# Probe GND spine and low-impedance ground route around the input resistor field.
a=pos('J1',2);b=pos('J1',16)
trace('GND',[a,b,(15,b[1]+2.46),(15,101),(65,101)],p.B_Cu,.8)
# Custom board graphics.
def line(a,b,layer=p.F_SilkS,width=.15):
 t=p.PCB_SHAPE();t.SetShape(p.SHAPE_T_SEGMENT);t.SetStart(V(*a));t.SetEnd(V(*b));t.SetLayer(layer);t.SetWidth(p.FromMM(width));board.Add(t)
def txt(s,x,y,size=1,layer=p.F_SilkS,angle=0):
 t=p.PCB_TEXT(board);t.SetText(s);t.SetPosition(V(x,y));t.SetTextSize(V(size,size));t.SetTextThickness(p.FromMM(.15));t.SetTextAngle(p.EDA_ANGLE(angle,p.DEGREES_T));t.SetLayer(layer);board.Add(t)
for a,b in [((0,0),(160,0)),((160,0),(160,110)),((160,110),(0,110)),((0,110),(0,0))]:line(a,b,p.Edge_Cuts,.05)
txt('LOGIC ANALYZER',48,4,1.8);txt('8 CH / PICO 2 / REV A1',48,7,1)
txt('INPUT PROTECTION',45,104,1.3);txt('USB ONLY',112.9,58,1.1)
txt('COMMON GND',9.5,69,1);txt('CH    GND',10.3,41,1)
for ch in range(8):txt(str(ch),6,44+2.54*ch,.9);txt(f'CH{ch}',18.8,14+11.2*ch,1)
for s,x,y in [('UP',133.25,78),('DOWN',150.25,78),('OK',133.25,95),('BACK',150.25,95),('RESET',135.25,50.5),('PWR',131,11),('STATUS',146,11),('CAL',152,44),('GND',152,54)]:txt(s,x,y,.95)
for i,s in enumerate(['GND','3V3','SCK','MOSI','RST','DC','CS','BL']):txt(s,132+2.54*i,14.7,.8,angle=90)
txt('J2: CHECK TFT CABLE PIN ORDER',140,63,.85)
txt('PICO 2 - SOCKETED',112.89,27,1.2,p.F_Fab,90)
txt('1x20 FEMALE / P2.54 / ROWS 17.78',112.89,54.5,.85)
txt('1k PR02 2W x2 / CHANNEL',42,108,1)
txt('Prototype - validate protection and TFT module before release',113,106,.8)
# Dimensions on Dwgs.User without polluting copper or silk.
for a,b in [((0,114),(160,114)),((-4,0),(-4,110))]:line(a,b,p.Dwgs_User,.15)
txt('160 mm',80,114,1.2,p.Dwgs_User);txt('110 mm',-4,55,1.2,p.Dwgs_User,90)
title=board.GetTitleBlock();title.SetTitle('Pico 2 Logic Analyzer Carrier - Layout A');title.SetRevision('A1');title.SetDate('2026-11-11');title.SetCompany('Logic Analyzer 2026.1')
# Ground zone on protected side only. Keep copper out from under the unprotected PR02 field.
z=p.ZONE(board);z.SetLayer(p.B_Cu);z.SetNet(netobjs['/GND']);z.SetLocalClearance(p.FromMM(.25));z.SetPadConnection(p.ZONE_CONNECTION_THERMAL);z.SetThermalReliefGap(p.FromMM(.3));z.SetThermalReliefSpokeWidth(p.FromMM(.4));z.SetMinThickness(p.FromMM(.2));z.SetIslandRemovalMode(p.ISLAND_REMOVAL_MODE_ALWAYS)
poly=z.Outline();poly.NewOutline()
for a in [(65,.8),(159.2,.8),(159.2,109.2),(65,109.2)]:poly.Append(V(*a))
board.Add(z)
p.SaveBoard(str(OUT),board)
# Export DSN before zone fill so the router sees all GND connections explicitly.
assert p.ExportSpecctraDSN(board,str(HERE/'review/unrouted.dsn'))
print(f'Placed {len(fp)} footprints; saved {OUT}; exported DSN.')
