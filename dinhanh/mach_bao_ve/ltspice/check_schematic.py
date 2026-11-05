#!/usr/bin/env python3
"""Check the .asc geometry/pins, and export its equivalent SPICE circuit.

Limited to the supplied R0 local symbols; not a general LTspice converter.
Does not require LTspice or Python packages.
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent
asc = (HERE/'input_protection_1ch.asc').read_text().splitlines()
wires, flags, instances, directives = [], [], [], []
for line in asc:
    if line.startswith('WIRE '):
        _,x,y,a,b = line.split(); wires.append(((int(x),int(y)),(int(a),int(b))))
    elif line.startswith('FLAG '):
        _,x,y,name = line.split(); flags.append(((int(x),int(y)),name))
    elif line.startswith('SYMBOL '):
        _,name,x,y,orient = line.split()
        assert orient == 'R0'
        instances.append({'symbol':name, 'origin':(int(x),int(y))})
    elif line.startswith('SYMATTR '):
        _,key,value = line.split(' ',2); instances[-1][key]=value
    elif line.startswith('TEXT ') and ' !' in line:
        directives.append(line.split(' !',1)[1])

points = {p for pair in wires for p in pair} | {p for p,_ in flags}
for item in instances:
    pins=[]; prefix=None
    for line in (HERE/(item['symbol']+'.asy')).read_text().splitlines():
        if line.startswith('SYMATTR Prefix '): prefix=line.split()[-1]
        if line.startswith('PIN '):
            _,x,y,*_=line.split()
            ox,oy=item['origin']; pins.append({'point':(ox+int(x),oy+int(y))})
        elif line.startswith('PINATTR SpiceOrder '):
            pins[-1]['order']=int(line.split()[-1])
    assert item['InstName'].startswith(prefix)
    item['pins']=sorted(pins,key=lambda p:p['order'])
    points.update(p['point'] for p in pins)

parent={p:p for p in points}
def find(p):
    if parent[p]!=p: parent[p]=find(parent[p])
    return parent[p]
def union(a,b): parent[find(a)]=find(b)
for a,b in wires:
    assert a[0]==b[0] or a[1]==b[1], 'Only orthogonal wires supported'
    for p in points:
        if (a[0]==b[0]==p[0] and min(a[1],b[1])<=p[1]<=max(a[1],b[1])) or (
            a[1]==b[1]==p[1] and min(a[0],b[0])<=p[0]<=max(a[0],b[0])):
            union(a,p)
labels={}
for p,name in flags:
    if name in labels: union(labels[name],p)
    else: labels[name]=p
names={find(p):name for name,p in labels.items()}
assert len(names)==len(labels), 'Different named nets shorted together'
for p in sorted(points):
    if find(p) not in names: names[find(p)]=f'N{len(names):03d}'
devices={}
for item in instances:
    nodes=[names[find(pin['point'])] for pin in item['pins']]
    devices[item['InstName']]=nodes
    assert len(set(nodes))==len(nodes),f'Shorted pins: {item["InstName"]}'
expected={
    'VIN':['IN','0'], 'R6':['IN','MID'], 'R7':['MID','CLAMP'],
    'R8':['CLAMP','A'], 'R9':['A','0'], 'XD3':['0','Z','CLAMP'],
    'D4':['0','Z'], 'C2':['VCC','0'], 'VCCSRC':['VCC','0'],
    'CLOAD':['OUT','0'],
}
for ref,nodes in expected.items(): assert devices[ref]==nodes,(ref,devices[ref],nodes)
assert devices['XU2'][0]=='A' and devices['XU2'][2:]==['VCC','0']
assert devices['R10']==[devices['XU2'][1],'OUT']
deck=['One-channel protection - exported from the actual ASC geometry']
for item in instances:
    deck.append(' '.join([item['InstName'],*devices[item['InstName']],item['Value']]))
deck+=directives
deck+=['.save V(IN) V(CLAMP) V(A) V(OUT) I(VIN)', '.end']
(HERE/'input_protection_1ch.cir').write_text('\n'.join(deck)+'\n')
print('PASS: one channel; all signal, diode, buffer, supply and ground connections match.')
print('Exported equivalent netlist: input_protection_1ch.cir')
