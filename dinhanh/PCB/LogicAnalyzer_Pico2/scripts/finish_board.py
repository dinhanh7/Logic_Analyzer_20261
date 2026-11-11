#!/usr/bin/env python3
"""Import local routing result and fill the protected-side GND plane."""
import pcbnew as p
from pathlib import Path
HERE=Path(__file__).resolve().parent.parent
fn=HERE/'LogicAnalyzer_Pico2.kicad_pcb';b=p.LoadBoard(str(fn))
assert p.ImportSpecctraSES(b,str(HERE/'review/routed.ses'))
for f in b.GetFootprints():
 for field in f.GetFields():
  if field.GetName() not in ('Reference',):field.SetVisible(False)
# Refill using KiCad's own zone engine.
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(fn),b)
print('Imported session and filled GND plane:',len(b.GetTracks()),'tracks/vias')
