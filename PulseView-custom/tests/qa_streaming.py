"""Lossless soak, fault recovery and explicit USB capacity acceptance tests."""
import json
import os
import signal
import subprocess
import sys
import time
import zipfile
from qa_common import ROOT,Blocked,require_run
import host_la
from stream_benchmark import Generator
from qa_offline import STACK,EXPECTED,sigrok,oracle_bytes


def register(ctx):
    import demo
    demo.stop_previous_demo()
    gen=Generator(ctx.args.esp32_port);log=[]
    def cmd(s):
        log.append(gen.command(s));ctx.save('C-generator.log','\n'.join(log))
    def run_stream(id,rate,seconds,raw=False,extra=()):
        path=ctx.out/(id+'.sr')
        command=['/usr/bin/time','-v',sys.executable,ROOT/'tools/stream_capture.py','--port',ctx.args.port,
                 '-r',rate,'-t',seconds,'-o',path]
        if raw:command+=['--no-rle']
        command+=list(extra)
        p=ctx.run(command,timeout=float(seconds)+45)
        ctx.save(id+'.log',p.stdout+p.stderr)
        meta=json.loads(path.with_suffix('.json').read_text());meta['exit_code']=p.returncode
        ctx.save(id+'-checked.json',meta)
        return path,meta
    def lossless(meta,seconds):
        assert meta['exit_code']==0 and meta['valid'] and not meta['dropped'],meta
        assert meta['received_samples']==meta['device_sent']==meta['device_captured'],meta
        assert meta['hardware_faults']==0,meta
        assert abs(meta['effective_sample_rate']/meta['rate']-1)<.001,meta
        assert abs(meta['capture_seconds']/seconds-1)<.001,meta
    def fresh():
        d=host_la.Device(ctx.args.port)
        try:
            d.recover();d.configure(1000000,1031,255);d.arm();data,info=d.capture(timeout=4)
            assert len(data)==1031;return info
        finally:d.close()
    try:
        for s in ('stress off','uart on','set uart 115200','set i2c 100','set spi 100','set can 250','burst on'):cmd(s)
        def c1():
            ctx.save('C1-snapshot-before-stream.json',fresh())
            path,m=run_stream('C1',1000000,10);lossless(m,10)
            for name in STACK:
                text=sigrok(ctx,path,name,'C1-'+name,'can=id' if name=='CAN' else None)
                if name=='CAN':assert 'Identifier: 291' in text
                else:
                    values=oracle_bytes(text,name);want=EXPECTED[name]
                    assert any(values[i:i+len(want)]==want for i in range(len(values)-len(want)+1)),(name,values[:20])
            return m
        ctx.case('C1','10s four-protocol streaming','1MHzRLE;SPI100k;CAN250k','0drop;counts/duration<0.1%;4bus oracle',c1)
        def c2():
            ctx.save('C2-snapshot-before-stream.json',fresh())
            path,m=run_stream('C2',500000,5,True);lossless(m,5)
            text=sigrok(ctx,path,'UART','C2-UART');values=oracle_bytes(text,'UART');want=EXPECTED['UART']
            assert any(values[i:i+len(want)]==want for i in range(len(values)-len(want)+1)),values[:20]
            return m
        ctx.case('C2','Raw USB streaming','500kHz5s;8bit/sample','0drop;counts<0.1%;UART correct',c2)
        def c3():
            if ctx.args.soak_seconds<300:
                raise Blocked('Full soak needs >=300s;this run requested %ds'%ctx.args.soak_seconds,status='NOT-VERIFIED')
            path,m=run_stream('C3',1000000,ctx.args.soak_seconds);lossless(m,ctx.args.soak_seconds)
            with zipfile.ZipFile(path) as z:
                assert z.getinfo('logic-1-1').file_size==m['samples'];assert z.testzip() is None
            ctx.save('C3-post-soak-recovery.json',fresh())
            return dict(**m,scope='PC RSS logged;firmware has static capture buffers;no MCU heap telemetry is exposed')
        ctx.case('C3','Continuous five-minute lossless soak','1MHzRLE;real bus traffic;>=300s','0drop/seq/CRC/fault;readable .sr;fresh capture',c3)
        def c4():
            rows=[]
            for name,sig in (('sigint',signal.SIGINT),('killed',signal.SIGKILL)):
                path=ctx.out/('C4-'+name+'.sr')
                with (ctx.out/('C4-'+name+'.log')).open('w') as f:
                    p=subprocess.Popen([sys.executable,str(ROOT/'tools/stream_capture.py'),'--port',ctx.args.port,
                                        '-r','1000000','-o',str(path)],cwd=ROOT,stdout=f,stderr=f)
                    time.sleep(1.5);assert p.poll() is None,'stream exited before injection'
                    p.send_signal(sig);p.wait(timeout=15)
                if sig==signal.SIGINT:
                    m=json.loads(path.with_suffix('.json').read_text());assert m['valid'] and not m['dropped'],m
                rows.append(dict(signal=name,exit=p.returncode,recovered=fresh()))
            ctx.save('C4-recovery.json',rows);return rows
        ctx.case('C4','Graceful stop and killed-host recovery','SIGINT and SIGKILL injected into real active host','SIGINT tail verified;after each fault capture valid',c4)
        def c5():
            cmd('stress 125000')
            try:path,m=run_stream('C5',500000,5,True,('--pause-seconds','.8'))
            finally:cmd('stress off')
            assert not m['valid'] and m['dropped']>0 and m['exit_code']!=0,m
            assert m['received_samples']==m['device_sent'] and m['dropped']==m['device_dropped'],m
            fresh()
            import capture_ui
            d=host_la.Device(ctx.args.port)
            try:
                d.recover();d.analyze(18,2,1);os.write(d.fd,b'o');time.sleep(.15)
                state=capture_ui.view_state(d);assert state['screen']==3,state
                capture_ui.save_frame(d,ctx.out/'C5-ui-recovered.png')
            finally:d.close()
            return dict(stream=m,ui=state,policy='TFT redraw intentionally deferred while streaming;responsive after drain')
        ctx.case('C5','Host backpressure and UI recovery','raw500k;square125k;PC read stall800ms','explicitINVALID/dropcounts;afterdrain nextscreen responds',c5)
        def c5b():
            cmd('stress 125000')
            before=time.monotonic()
            try:path,m=run_stream('C5b',500000,7,True,('--pause-seconds','4'))
            finally:cmd('stress off')
            assert not m['valid'] and m['exit_code']!=0,m
            elapsed=time.monotonic()-before
            assert elapsed<15,elapsed
            recovered=fresh()
            ctx.save('C5b-recovered.json',recovered)
            return dict(stream=m,recovered=recovered,seconds=elapsed,
                        policy='4s stall exceeds send_frame3s deadline;integrity error is explicit;HELLO recovers')
        ctx.case('C5b','USB send deadline under prolonged host stall','raw500k;4s unread interval exceeds3s deadline','explicitINVALID;no permanent hang;freshcapture<15s',c5b)
        def c6():
            rows=[]
            for rate in (2000000,5000000):
                p=ctx.run([sys.executable,ROOT/'tools/stream_capture.py','--port',ctx.args.port,'-r',rate,
                           '-t','1','-o',ctx.out/('C6-%d.sr'%rate)])
                ctx.save('C6-%d.log'%rate,p.stdout+p.stderr)
                assert p.returncode!=0 and 'USB CDC supported rates' in p.stderr
                rows.append(dict(rate=rate,exit=p.returncode))
            d=host_la.Device(ctx.args.port)
            try:
                d.recover();d.configure(5000000,262144,255)
                try:d.analyze(16,1,0)
                except RuntimeError as exc:rows.append(dict(device_rate=5000000,rejection=str(exc)))
                else:raise AssertionError('device accepted5MHz stream')
                d.recover()
            finally:d.close()
            return rows
        ctx.case('C6','Unsupported rates cannot silently succeed','2MHz/5MHz stream requests','explicit error;no invalid VALID file',c6)
    finally:
        try:
            for s in ('stress off','set spi 1000','set can 250','uart on','burst on'):cmd(s)
        finally:gen.close()
