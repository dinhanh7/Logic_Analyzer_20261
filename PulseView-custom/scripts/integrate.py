#!/usr/bin/env python3
"""Install the maintained driver overlay into the pinned libsigrok checkout."""
from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[1]
src=root/'third_party/libsigrok'
dest=src/'src/hardware/rp2350-la'
dest.mkdir(parents=True,exist_ok=True)
for p in (root/'driver').iterdir():
    if p.is_file(): shutil.copy2(p,dest/p.name)
shutil.copy2(root/'protocol/lar2.h',dest/'lar2.h')
p=src/'configure.ac'; text=p.read_text()
line='SR_DRIVER([RP2350 Logic Analyzer], [rp2350-la])'
if line not in text: p.write_text(text.replace('SR_DRIVER([demo], [demo])','SR_DRIVER([demo], [demo])\n'+line))
p=src/'Makefile.am'; text=p.read_text()
if 'if HW_RP2350_LA' not in text:
    p.write_text(text+'\nif HW_RP2350_LA\nsrc_libdrivers_la_SOURCES += src/hardware/rp2350-la/api.c src/hardware/rp2350-la/protocol.c src/hardware/rp2350-la/acquisition.c src/hardware/rp2350-la/protocol.h src/hardware/rp2350-la/lar2.h\nendif\n')
print('Integrated rp2350-la')
