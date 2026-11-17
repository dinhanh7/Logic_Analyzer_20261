"""Repeated acquisition, real USB software reset and the legacy regression gate."""
import os
import struct
import zlib
import resource
import subprocess
import sys
import time
from qa_common import ROOT,require_run
import host_la
import demo


def register(ctx):
    demo.stop_previous_demo()
    def f1():
        dev=host_la.Device(ctx.args.port);rows=[]
        try:
            dev.hello()
            for n in range(50):
                dev.configure(1000000,65536,255);t=time.monotonic();dev.arm();data,info=dev.capture(timeout=4)
                assert len(data)==65536 and info['trigger_index']==0xffffffff
                rows.append(dict(iteration=n,seconds=time.monotonic()-t,peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss))
            ctx.save('F1-repeated-captures.json',rows)
        finally:dev.close()
        return dict(captures=len(rows),max_seconds=max(r['seconds'] for r in rows),host_peak_rss_kib=rows[-1]['peak_rss_kib'],
                    limitation='No MCU heap watermark API;successful repetition does not prove absence of all leaks')
    ctx.case('F1','50 consecutive independent snapshots','manual1MHz65536;sameDevice session','50/50CRC/sequence/metadata correct',f1)
    def f2():
        import serial
        command=[str(ROOT/'scripts/run-sigrok-cli.sh'),'-l','3','-d','rp2350-la:conn='+ctx.args.port,
                 '--config','samplerate=1000','--samples','262144','-o',str(ctx.out/'F2-interrupted.sr')]
        proc=subprocess.Popen(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            time.sleep(.8);assert proc.poll() is None,'capture did not stay active before reset'
            # Pico SDK's1200baud bootloader request physically reenumerates USB.
            with serial.Serial(ctx.args.port,1200,timeout=.1):pass
            stdout,stderr=proc.communicate(timeout=10)
            ctx.save('F2-disconnect.log',stdout+stderr)
            assert 'Capture verified:' not in stderr,'interrupted capture advertised valid'
        finally:
            if proc.poll() is None:proc.kill();proc.communicate()
        p=ctx.run([sys.executable,ROOT/'tools/flash_pico.py'],timeout=75)
        ctx.save('F2-reset-reflash.log',p.stdout+p.stderr);require_run(p)
        dev=host_la.Device(ctx.args.port)
        try:
            ident=dev.hello();dev.configure(1000000,1031,255);dev.arm();data,info=dev.capture(timeout=4)
            assert len(data)==1031;ctx.save('F2-recovered.json',dict(board=ident,info=info))
        finally:dev.close()
        return 'actual USB reenumeration during active driver capture;error not marked valid;reflash/reconnect capture passed'
    ctx.case('F2','Mid-session hardware USB reset/recovery','SDK1200baud reset into ROMbootloader;reload currentUF2','old acquisition invalid;driver/host reconnect and capture valid',f2)
    def f4():
        import lar2
        from test_storage_hardware import replay
        dev=host_la.Device(ctx.args.port)
        try:
            dev.hello();headers=[];valid=[];invalid=[]
            for slot in (0,1):
                raw=dev.analyze(6,slot,strip_status=False)
                values=struct.unpack('<IIQIII4B',raw)
                headers.append(dict(slot=slot,header_hex=raw.hex(),count=values[1]))
                if values[0]==0x4c415345 and 0<values[1]<=262144:valid.append(slot)
                else:invalid.append(slot)
            ctx.save('F4-original-headers.json',headers)
            if not valid:
                from qa_common import Blocked
                raise Blocked('No valid existing flash session;no user slot was overwritten')
            slot=valid[0];dev.load_session(slot);original,info=replay(dev)
            ctx.samples('F4-read-only-session',original,info['rate'])
            dev.save_golden();assert dev.compare_golden().startswith(b'SIG: MATCH')
            assert dev.diff_session(slot).startswith(b'DIFF: identical')
            dev.configure(1000000,3,255);dev.arm();tiny,ti=dev.capture()
            dev.save_golden();assert dev.compare_golden().startswith(b'SIG: MATCH')
            assert dev.diff_session(slot).startswith(b'DIFF: first mismatch')
            assert dev.compare_golden().startswith(b'SIG: MATCH'),'DIFF destroyed the golden reference'
            dev.load_session(slot);restored,ri=replay(dev);assert restored==original
            for bad in invalid:
                try:dev.load_session(bad)
                except RuntimeError:pass
                else:raise AssertionError('invalid flash header accepted')
                retained,_=replay(dev);assert retained==original,'rejected LOAD changed RAM'
            result=dict(slot=slot,count=len(original),crc32='%08x'%zlib.crc32(original),
                        odd_length=3,invalid_headers_rejected=invalid,flash_writes=0,
                        scope='load/replay/golden/diff checked;new save/erase skipped to preserve occupied user slots')
            ctx.save('F4-read-only-storage.json',result);return result
        finally:dev.close()
    ctx.case('F4','Non-destructive flash/golden/diff regression','existing flash session;3sample comparison;invalid header;noerase','byte-exact replay;DIFF preserves golden;invalidLOAD retains RAM',f4)
    def f3():
        for name,command in [('legacy',['bash','scripts/test.sh']),('esp32',[sys.executable,'tests/test_esp32_link.py'])]:
            p=ctx.run(command,timeout=240);ctx.save('F3-'+name+'.log',p.stdout+p.stderr);require_run(p)
        failed=[r['id'] for r in ctx.rows if r['status']=='FAIL']
        assert not failed,failed
        return 'legacy suite and4protocol x snapshot/streaming PASS;new runner checks all executed cases;blocked criteria remain explicit'
    ctx.case('F3','Full regression gate','oldsoftware tests+realESP32test+currentQAresults','no executedFAIL;never relabel BLOCKED as PASS',f3)
