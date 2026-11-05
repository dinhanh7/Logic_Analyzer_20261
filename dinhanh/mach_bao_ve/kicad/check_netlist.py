#!/usr/bin/env python3
"""Check physical pin connectivity in KiCad's exported XML netlist."""
from pathlib import Path
import xml.etree.ElementTree as ET

root = ET.parse(Path(__file__).with_name('input_protection.net.xml'))
nets = {n.attrib['name'].lstrip('/'):
        {(x.attrib['ref'], x.attrib['pin']) for x in n.findall('node')}
        for n in root.findall('./nets/net')}
for i in range(8):
    r = lambda k: f'R{5*i+k}'
    d = lambda k: f'D{2*i+k}'
    u = f'U{i+1}'
    assert nets[f'CH{i}_IN'] == {('J1', str(i+1)), (r(1), '1')}
    assert nets[f'CLAMP_{i}'] == {(r(2), '2'), (r(3), '1'), (d(1), '3')}
    assert nets[f'Z_{i}'] == {(d(1), '2'), (d(2), '1')}
    assert nets[f'A_{i}'] == {(r(3), '2'), (r(4), '1'), (u, '2')}
    assert nets[f'CH{i}_OUT'] == {(r(5), '2'), ('J2', str(i+1))}
    assert {(d(1), '1'), (d(2), '2'), (u, '3')} <= nets['GND']
    assert (u, '5') in nets['+3V3']
    assert not any(ref.startswith('D') for ref, _ in nets['+3V3'])
    assert any({(r(1), '2'), (r(2), '1')} <= nodes for nodes in nets.values())
    assert any({(u, '4'), (r(5), '1')} <= nodes for nodes in nets.values())
print('PASS: 8 channels, physical connector/diode/buffer pin mappings; no clamp diode on +3V3.')
