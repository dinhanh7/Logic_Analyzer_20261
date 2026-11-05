#!/usr/bin/env python3
"""Exploratory ngspice front-end simulation; NOT vendor-qualified models.

Requires ngspice, numpy and matplotlib. Set NGSPICE to the executable path.
Generated .cir files can also be run individually with ngspice -b <file>.
"""
from pathlib import Path
import csv
import os
import subprocess
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
NGSPICE = os.environ.get('NGSPICE', 'ngspice')
results = []
waves = {}

# Representative diode parameters, not vendor SPICE models. BV/IBV anchor
# the Zener at its datasheet test point; Cjo is a stated assumption.
MODELS = '''
.model DBAV D(Is=2.52n N=1.75 Rs=.568 Cjo=1.5p Tt=4n Bv=100 Ibv=.5u)
.model DZENER D(Is=1n N=1.5 Rs=2 Cjo=500p Bv=3.3 Ibv=.08)
'''

def simulate(name, source, step, stop, extra_cap=2e-12):
    dest = HERE / f'{name}.dat'
    deck = f'''Exploratory protection model - {name}
* Standalone one-channel equivalent of the supplied KiCad schematic.
* +3V3 supply is intentionally absent from the passive clamp network.
Vdut in 0 {source}
R1 in mid 1000
R2 mid clamp 1000
Dhi clamp z DBAV
Dlo 0 clamp DBAV
Dz 0 z DZENER
R3 clamp a 330
R4 a 0 47000
Ca a 0 6p
Cstray clamp 0 {extra_cap}
{MODELS}
.control
set wr_singlescale
set wr_vecnames
tran {step} {stop} 0 {step}
wrdata {dest.as_posix()} v(in) v(clamp) v(a) v(z) i(Vdut) v(mid)
quit
.endc
.end
'''
    path = HERE / f'{name}.cir'; path.write_text(deck)
    proc = subprocess.run([NGSPICE, '-b', str(path)], text=True, capture_output=True)
    (HERE / f'{name}.log').write_text(proc.stdout + proc.stderr)
    if proc.returncode != 0 or not dest.exists():
        raise RuntimeError(proc.stdout + proc.stderr)
    data = np.loadtxt(dest, skiprows=1)
    # time, in, clamp, a, z, current, mid
    t, vin, clamp, a, z, current, mid = data.T
    pr1 = (vin-mid)**2/1000
    row = {'case':name, 'A_max_V':float(a.max()), 'A_min_V':float(a.min()),
        'CLAMP_max_V':float(clamp.max()), 'I_source_max_A':float(abs(current).max()),
        'R1_peak_W':float(pr1.max()),
        'R1_energy_J':float(np.trapezoid(pr1,t)),
        'A_final_V':float(a[-1])}
    results.append(row); waves[name] = data
    return data

simulate('dc_12V', 'DC 12', 1e-7, 1e-4)
simulate('pulse_200V_1ms', 'PULSE(0 200 10u 20n 20n 1m 10m)', 1e-7, 1.1e-3)
for amplitude in (3.3, 5, 12):
    for freq in (1000,100000,1000000,3000000,5000000):
        period=1/freq; edge=min(5e-9,period/100)
        width=period/2-edge
        data=simulate(f'square_{amplitude:g}V_{freq:g}Hz',
            f'PULSE(0 {amplitude} {period} {edge} {edge} {width} {period})',
            period/600,12*period)
        t,vin,clamp,a,z,current,mid=data.T
        last=t>=8*period
        # Check both input thresholds, not just visual edge sharpening.
        results[-1]['late_A_max_V']=float(a[last].max())
        results[-1]['late_A_min_V']=float(a[last].min())
        results[-1]['crosses_2p4V_and_0p8V']=bool(a[last].max()>2.4 and a[last].min()<0.8)

fields=list(dict.fromkeys(k for row in results for k in row))
with (HERE/'results.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(results)

fig,axs=plt.subplots(3,1,figsize=(11,9))
data=waves['pulse_200V_1ms']; t,vin,clamp,a,z,current,mid=data.T
axs[0].plot(t*1e3,clamp,label='CLAMP'); axs[0].plot(t*1e3,a,label='Buffer input A')
axs[0].axhline(5.5,color='red',ls='--',label='LVC input recommended max')
axs[0].set(xlabel='Time (ms)',ylabel='Voltage (V)',title='+200 V / 1 ms: exploratory clamp model')
for ax,amp in zip(axs[1:],(3.3,12)):
    data=waves[f'square_{amp:g}V_1e+06Hz']; t,vin,clamp,a,z,current,mid=data.T
    use=(t>=8e-6)&(t<=11e-6)
    ax.plot(t[use]*1e6,vin[use],label='DUT input')
    ax.plot(t[use]*1e6,a[use],label='Buffer input A')
    ax.axhline(2.4,color='gray',ls='--',label='Threshold check 2.4 V')
    ax.axhline(.8,color='gray',ls=':')
    ax.set(xlabel='Time (us)',ylabel='Voltage (V)',title=f'1 MHz, 0...{amp:g} V; analog input before Schmitt buffer')
for ax in axs:
    ax.grid(alpha=.25); ax.legend(loc='upper right')
fig.suptitle('Representative models only - no component-tolerance or PCB qualification',fontsize=11)
fig.tight_layout(); fig.savefig(HERE/'waveforms.png',dpi=170)
print(f'Completed {len(results)} cases. See results.csv and waveforms.png.')
