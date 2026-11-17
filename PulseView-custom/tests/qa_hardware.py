"""Evidence-based hardware/BOM assessment; absence is never a PASS."""
import re
from pathlib import Path
from qa_common import ROOT,Blocked


def register(ctx):
    tracked=ctx.run(['git','-C',ROOT.parent,'ls-files']).stdout.splitlines()
    candidates=[p for p in tracked if re.search(r'bom|kicad|schematic|protect|\.sch$|\.pcb$|\.xlsx$',p,re.I)]
    ctx.save('E-inventory.json',dict(candidates=candidates,scope='tracked project files;topic2 plan is not a schematic or measurement'))
    historical=ROOT/'docs/history/HARDWARE-PLAN-2026-10.md'
    if historical.exists():ctx.save('E-historical-design-plan.md',historical.read_text())
    def e1():
        ctx.save('E1-protection-assessment.md',
            '# Protection assessment\n\nHistorical PLAN.md proposes47k series/BAT54S/SMAJ5V0A for4channels;it is not a measured8-channel front-end. No measured protection transfer function, pulse source waveform, clamp current/voltage, '\
            'pulse width/source impedance, input schematic/netlist or rated component evidence was located in maintained sources. '\
            'The historical topic2 plan says protection existed, which is not measurement evidence. '\
            'Direct RP2350 GPIO must not be treated as 5V/12V/200V tolerant. No high-voltage injection was performed.\n')
        raise Blocked('CHƯA KIỂM CHỨNG BẰNG ĐO:missing schematic/component ratings and safe measured12V/200V front-end evidence','NOT-VERIFIED')
    ctx.case('E1','12VDC/200Vpulse protection','source inventory and evidence review;no safe pulse fixture','rated schematic plus measured clamp behavior required',e1)
    def e2():
        ctx.save('E2-BOM-assessment.md','# BOM assessment\n\nBudget target:450000VND. Historical workspace PLAN.md lists Pico180-230k,OLED65-88k,LVC24510-20k,protection30k,accessories50-80k,LED3-5k VND. Summing the listed non-ST-Link rows gives338000-453000VND, not its claimed363000-440000VND. This is a4-channel/OLED proposal,not the current8-channel/TFT as-built BOM. Supplier/date estimates are not receipts or proof of assembled cost.\n')
        raise Blocked('Historical4-channel/OLED estimate sums338000-453000VND;current8-channel/TFT as-built BOM/receipts missing;<=450000not verified','NOT-VERIFIED')
    ctx.case('E2','Budget conformity','priced BOM inventory','quantity*price total<=450000VND with traceable prices',e2)
    def e3():
        data=dict(channels=8,snapshot_rate_max_hz=25000000,resolution_ns=40,
                  manual_samples_max=262144,manual_window_ms=262144/25000,
                  triggered_samples_max=131072,trigger_window_ms=131072/25000,
                  demo_samples=65536,demo_window_ms=65536/25000,
                  stream_rate_max_hz=1000000,raw_recommended_hz=500000,
                  streaming_policy='RLE1MHz for compressible traffic; dense/raw1MHz can overrun; strict counters expose loss',
                  unknown=['absolute clock accuracy versus calibrated instrument','channel skew/crosstalk','input resistance/capacitance/bandwidth','5V front-end compatibility','12V/200V protection'])
        ctx.save('E3-practical-limits.json',data)
        raise Blocked('Digital limits are derived/tested;analog loading,5V compatibility and absolute calibration lack measurements;see E3-practical-limits.json','NOT-VERIFIED')
    ctx.case('E3','Practical operating envelope','code+measured digital paths;analog fixture absent','state40ns/buffer/USB limits;measure loading/voltage before compatibility claim',e3)
