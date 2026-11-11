#!/usr/bin/env python3
"""Post-route engineering corrections and silkscreen cleanup. Safe to re-run."""
import pcbnew as p
from pathlib import Path
HERE=Path(__file__).resolve().parent.parent
fn=HERE/'LogicAnalyzer_Pico2.kicad_pcb';b=p.LoadBoard(str(fn));fp={f.GetReference():f for f in b.GetFootprints()}
def V(x,y):return p.VECTOR2I(p.FromMM(x),p.FromMM(y))
def pad(r,n):return next(x for x in fp[r].Pads() if x.GetNumber()==str(n))
def pos(r,n):v=pad(r,n).GetPosition();return p.ToMM(v.x),p.ToMM(v.y)
def trace(net,points,layer=p.F_Cu,width=.25):
 net=b.FindNet('/'+net)
 for a,c in zip(points,points[1:]):
  # Avoid exact duplicate segments on rerun.
  if any(isinstance(t,p.PCB_TRACK) and t.GetStart()==V(*a) and t.GetEnd()==V(*c) and t.GetLayer()==layer for t in b.GetTracks()):continue
  t=p.PCB_TRACK(b);t.SetStart(V(*a));t.SetEnd(V(*c));t.SetWidth(p.FromMM(width));t.SetLayer(layer);t.SetNet(net);b.Add(t)
for t in list(b.GetTracks()):
 if isinstance(t,p.PCB_VIA):
  if t.GetWidth(p.F_Cu)-t.GetDrillValue()<p.FromMM(.3):t.SetDrill(p.FromMM(.3))
 elif t.GetWidth()<p.FromMM(.2):t.SetWidth(p.FromMM(.2))
# Complete three missing connections confirmed by KiCad DRC, not by router estimates.
for t in list(b.GetTracks()):
 if not isinstance(t,p.PCB_VIA) and t.GetNetname()=='/CLAMP_0' and t.GetStart()==V(68,10) and t.GetEnd()==V(68,14):b.RemoveNative(t)
trace('CLAMP_0',[pos('TP13',1),(67.5,10.5),(67.5,13.5),(68,14)])
trace('GND',[pos('D16',2),(61.14,101),(65,101)],p.B_Cu,.5)
trace('GND',[pos('U9',18),pos('U9',23)],p.F_Cu,.5)
# Remove redundant via flagged by DRC; its net is entirely connected on front copper.
for t in list(b.GetTracks()):
 if isinstance(t,p.PCB_VIA) and t.GetNetname()=='/BL_PWR':b.RemoveNative(t)
# Slightly narrower thermal spokes fit between the Pico fanout traces.
for z in b.Zones():z.SetThermalReliefSpokeWidth(p.FromMM(.25));z.SetThermalReliefGap(p.FromMM(.25))
# Reference positions on the component side, keeping labels out of solder openings.
def ref(r,x,y):f=fp[r].Reference();f.SetPosition(V(x,y));f.SetTextAngle(p.EDA_ANGLE(0,p.DEGREES_T))
for r,x,y in [('J2',129,18),('D17',131,5),('D18',146,5),('Q1',135,29),('Q2',146,29),('R44',135,38),('R46',146,38),('TP12',19,11.5),('TP15',75.5,20.6)]:ref(r,x,y)
# Direct plane connection for crowded socket GND pads; no starved spokes.
for n in (8,13,28):pad('U9',n).SetLocalZoneConnection(p.ZONE_CONNECTION_FULL)
# Replace the temporary ground strap that crossed the SPI clock.
remove_segments={(104.0,20.98,106.0,20.98),(106.0,20.98,120.0,20.98),(120.0,20.98,121.78,20.98)}
for t in list(b.GetTracks()):
 if isinstance(t,p.PCB_VIA) or t.GetNetname()!='/GND' or t.GetLayer()!=p.F_Cu:continue
 a=t.GetStart();c=t.GetEnd();key=tuple(round(p.ToMM(v),3) for v in (a.x,a.y,c.x,c.y))
 if key in remove_segments:b.RemoveNative(t)
trace('GND',[pos('U9',13),(105.5,32.18),(105.5,30.5)],p.F_Cu,.4)
if not any(isinstance(t,p.PCB_VIA) and t.GetPosition()==V(105.5,30.5) for t in b.GetTracks()):
 v=p.PCB_VIA(b);v.SetPosition(V(105.5,30.5));v.SetWidth(p.FromMM(.8));v.SetDrill(p.FromMM(.4));v.SetViaType(p.VIATYPE_THROUGH);v.SetLayerPair(p.F_Cu,p.B_Cu);v.SetNet(b.FindNet('/GND'));b.Add(v)
for item in b.GetDrawings():
 if isinstance(item,p.PCB_TEXT) and item.GetText().startswith('CH') and item.GetText()[2:].isdigit():
  ch=int(item.GetText()[2:]);item.SetPosition(V(18.8,17+11.2*ch))
# Extra fields must never appear on silk.
for f in b.GetFootprints():
 for field in f.GetFields():
  if field.GetName()!='Reference':field.SetVisible(False)
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(fn),b);print('Refined copper, thermal connections and references.')
