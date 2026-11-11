#!/usr/bin/env python3
"""Add an illustrative PR02 body/lead model; dimensions are in mm / 2.54 (VRML).
This is a visualization, not a supplier mechanical STEP for tolerance checks.
"""
import math,pcbnew as p
from pathlib import Path
HERE=Path(__file__).resolve().parent.parent
chunks=['#VRML V2.0 utf8\n# Illustrative PR02, 10x3.9mm body, pitch17.78mm, 0.78mm wire.\n']
def cylinder(center,length,radius,color,axis='x'):
 vertices=[];faces=[];n=32
 for end in (-1,1):
  for i in range(n):
   theta=2*math.pi*i/n;c=radius*math.cos(theta);d=radius*math.sin(theta)
   local=(end*length/2,c,d) if axis=='x' else (c,d,end*length/2)
   vertices.append(tuple((a+v)/2.54 for a,v in zip(center,local)))
 vertices += [tuple((a+v)/2.54 for a,v in zip(center,(-length/2,0,0) if axis=='x' else (0,0,-length/2))),tuple((a+v)/2.54 for a,v in zip(center,(length/2,0,0) if axis=='x' else (0,0,length/2)))]
 for i in range(n):
  j=(i+1)%n;faces += [(i,j,j+n,i+n),(2*n,j,i),(2*n+1,i+n,j+n)]
 points=', '.join(' '.join(f'{x:.7f}' for x in v) for v in vertices)
 indexes=', '.join(' '.join(str(x) for x in v)+' -1' for v in faces)
 chunks.append(f'Shape {{ appearance Appearance {{ material Material {{ diffuseColor {color} }} }} geometry IndexedFaceSet {{ solid FALSE creaseAngle 1.57 coord Coordinate {{ point [ {points} ] }} coordIndex [ {indexes} ] }} }}\n')
cylinder((8.89,0,3.15),10,1.95,'.58 .13 .09')
for x in (0,17.78):cylinder((x,0,.725),4.85,.39,'.72 .72 .73','z')
for x in (1.945,15.835):cylinder((x,0,3.15),3.89,.39,'.72 .72 .73')
for x,color in [(4.7,'.20 .08 .02'),(5.8,'.02 .02 .02'),(6.9,'.02 .02 .02'),(8.0,'.20 .08 .02'),(12.8,'.20 .08 .02')]:cylinder((x,0,3.15),.45,1.96,color)
(HERE/'models/PR02_Illustrative.wrl').write_text(''.join(chunks))
path='${KIPRJMOD}/models/PR02_Illustrative.wrl'
f=HERE/'LogicAnalyzer.pretty/PR02_1k_2W_P17.78.kicad_mod'
s=f.read_text().rstrip()
if path not in s:s=s[:-1]+f'\n(model "{path}" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0))))\n';f.write_text(s)
fn=HERE/'LogicAnalyzer_Pico2.kicad_pcb';b=p.LoadBoard(str(fn))
for f in b.GetFootprints():
 if f.GetFPID().GetLibItemName()=='PR02_1k_2W_P17.78' and not any(m.m_Filename==path for m in f.Models()):
  m=p.FP_3DMODEL();m.m_Filename=path;m.m_Show=True;f.Models().push_back(m)
p.SaveBoard(str(fn),b);print('Added illustrative PR02 models (16 resistors).')
