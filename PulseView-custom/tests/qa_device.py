"""Real Pico/ESP32 snapshot, timing and repeated-trigger acceptance tests."""
import csv
import json
import math
import resource
import statistics
import time
from qa_common import ROOT,Blocked
import host_la
import lar2
from stream_benchmark import Generator
from qa_offline import STACK,EXPECTED,oracle_bytes,sigrok


def timing(data,rate,ch=0):
    edges=[i for i in range(1,len(data)) if (data[i]>>ch)&1 != (data[i-1]>>ch)&1]
    rises=[i for i in edges if (data[i]>>ch)&1]
    periods=[b-a for a,b in zip(rises,rises[1:])]
    assert len(periods)>=2,'insufficient periods'
    mean=statistics.mean(periods)
    span=data[rises[0]:rises[-1]]
    return dict(periods=len(periods),period_samples_mean=mean,frequency_hz=rate/mean,
                duty=sum((v>>ch)&1 for v in span)/len(span),
                jitter_pp_samples=max(periods)-min(periods),jitter_rms_samples=statistics.pstdev(periods),
                min_period_samples=min(periods),max_period_samples=max(periods))


def register(ctx):
    import demo
    demo.stop_previous_demo()
    gen=Generator(ctx.args.esp32_port);dev=host_la.Device(ctx.args.port);transcript=[]
    def cmd(text):
        reply=gen.command(text);transcript.append(reply);ctx.save('B-generator.log','\n'.join(transcript));return reply
    def capture(rate=25000000,count=65536,pre=32768,ch=0,edge=1,mode=1,value=0,mask=255):
        dev.recover();dev.configure(rate,count,255,pre if mode else 0,(mode,ch,edge,value,mask) if mode else None)
        t=time.monotonic();dev.arm();data,info=dev.capture(timeout=4)
        return data,info,time.monotonic()-t
    try:
        dev.hello()
        for c in ('stress off','uart on','set uart 115200','set i2c 100','set spi 1000','set can 250','burst on'):cmd(c)
        def b1():
            cmd('freq 1000');cmd('stress 1000');rows=[]
            try:
                for rate in (1000000,5000000,25000000):
                    data,info,_=capture(rate,262144,mode=0)
                    ctx.samples('B1-%d'%rate,data,rate);m=timing(data,rate)
                    m.update(rate=rate,error_percent=(m['frequency_hz']/1000-1)*100)
                    rows.append(m);assert abs(m['error_percent'])<.5,m
                    assert abs(m['duty']-.5)<.005,m
                ctx.save('B1-measurements.json',rows)
                return dict(reference='ESP32 LEDC timer2 on existing CH0 wire;not external calibration',measurements=rows)
            finally:cmd('stress off')
        ctx.case('B1','Frequency, duty and quantization jitter','known1kHz PWM on GPIO26;1/5/25MHz','frequency error<0.5%;record period/duty/jitter',b1)
        def b2():
            rows=[]
            for rate in (1000000,5000000,25000000):
                data,info,_=capture(rate)
                ctx.samples('B2-%d'%rate,data,rate)
                edges=[i for i in range(1,len(data)) if (data[i]^data[i-1])&1]
                theory=rate/115200
                runs=[b-a for a,b in zip(edges,edges[1:]) if .7*theory<=b-a<=1.3*theory]
                assert len(runs)>=5,runs
                measured=statistics.mean(runs);err=(measured/theory-1)*100
                row=dict(rate=rate,expected_samples=theory,mean_samples=measured,count=len(runs),error_percent=err,
                         jitter_pp_samples=max(runs)-min(runs),jitter_rms_samples=statistics.pstdev(runs))
                rows.append(row);assert abs(err)<2,row
                if rate==25000000:assert abs(measured-theory)<1,row
            ctx.save('B2-bit-width.json',rows);return rows
        ctx.case('B2','Actual UART bit-width metrology','UART115200;1/5/25MHz;unit-width run clusters','mean<2%;25MHz mean<1sample error',b2)
        def b3():
            rows=[];missing=[]
            cmd('set spi 100') # avoid1MHz SCK alias during the activity precheck
            for ch in range(8):
                # A flat input cannot exercise both trigger polarities.
                data,_,_=capture(1000000,262144,mode=0)
                active=any((a^b)&(1<<ch) for a,b in zip(data,data[1:]))
                if not active:missing.append(ch);continue
                for edge in (0,1):
                    data,info,elapsed=capture(ch=ch,edge=edge)
                    index=info['trigger_index'];before=(data[index-1]>>ch)&1;after=(data[index]>>ch)&1
                    row=dict(channel=ch,edge='falling' if edge else 'rising',index=index,seconds=elapsed,before=before,after=after)
                    rows.append(row);ctx.save('B3-edge-results.json',rows)
                    assert abs(index-32768)<=1,row
                    assert (before,after)==((1,0) if edge else (0,1)),row
            if missing:raise Blocked('Tested %d polarities; no external transitions on CH%s; see B3-edge-results.json'%(len(rows),missing))
            return rows
        ctx.case('B3','All-channel edge trigger geometry','8channels x2polarities;25MHz;65536/pre32768','exact32768±1 and correct previous/current bit',b3)
        def b4():
            data,info,_=capture(mode=2,value=1,mask=1)
            index=info['trigger_index'];assert data[index]&1
            manual,mi,_=capture(1000000,1031,mode=0);assert mi['trigger_index']==0xffffffff
            cmd('uart off') # Native UART TX strongly holds high: no floating-input assumption.
            time.sleep(.05)
            stable,_,_=capture(1000000,262144,mode=0)
            assert all(v&1 for v in stable),'UART-off reference must remain high'
            ctx.samples('B4-driven-idle-reference',stable,1000000)
            levels=1;opposite=0
            try:
                dev.recover();dev.configure(25000000,65536,255,32768,(2,0,0,0,1));dev.arm()
                try:
                    unexpected,unexpected_info=dev.capture(.4)
                except TimeoutError:pass
                else:
                    ctx.samples('B4-unexpected',unexpected,unexpected_info['rate'])
                    ctx.save('B4-unexpected.json',dict(info=unexpected_info,first=list(unexpected[:16]),
                                                     at_trigger=unexpected[unexpected_info['trigger_index']]))
                    raise AssertionError('strongly driven UART-idle-low pattern unexpectedly matched')
                dev.recover();dev.configure(1000000,1031,255);dev.arm();again,ri=dev.capture()
                assert len(again)==1031
            finally:dev.recover();cmd('uart on')
            ctx.save('B4-pattern-manual.json',dict(pattern=info,manual=mi,recovery=ri,reference_level=bool(levels&1),never_match_value=opposite))
            return 'matching pattern and manual valid;never-match host timeout+HELLO recovery valid'
        ctx.case('B4','Pattern/manual and never-match recovery','CH0high pattern;UART-off driven-high CH0 opposite pattern;400ms host timeout','match correct;manual no trigger;subsequent capture valid',b4)
        def b5():
            rows=[]
            for count,pre in ((65536,0),(65536,32768),(65536,60000),(262144,131072)):
                data,info,elapsed=capture(count=count,pre=pre)
                rows.append(dict(request_count=count,request_pre=pre,info=info,seconds=elapsed))
                ctx.save('B5-clamps.json',rows)
                assert len(data)==min(count,131072)
                assert info['pretrigger']<=len(data)//2,rows[-1]
                assert info['trigger_index']==min(pre,len(data)//2),rows[-1]
            return rows
        ctx.case('B5','Buffer and pretrigger boundary clamp','pre0/half/>half;count262144triggered','count<=131072;pre<=half;exact index;no deadlock',b5)
        def b6():
            rows=[];rss=[]
            for n in range(100):
                data,info,elapsed=capture()
                row=dict(iteration=n,index=info['trigger_index'],seconds=elapsed);rows.append(row)
                ctx.save('B6-100-captures.json',rows)
                assert elapsed<1 and info['trigger_index']==32768,row
                rss.append(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
            return dict(captures=len(rows),max_seconds=max(r['seconds'] for r in rows),host_peak_rss_kib=max(rss))
        ctx.case('B6','Repeated sparse trigger','UARTburst100ms;100 consecutive captures','100/100;index32768;eachARM-to-completion<1s',b6)
        def b7():
            rows=[]
            wants={'UART':'LA1234','I2C':'0x68 W ACK 0x01 ACK 0x5A ACK','SPI':'DE AD BE EF 00 01 02 03','CAN':'CAN ID=0x123 DLC=4'}
            for rate in (1000000,5000000,25000000):
                cmd('set spi 100' if rate==1000000 else 'set spi 1000')
                for name,ch in (('UART',0),('I2C',1),('SPI',3),('CAN',7)):
                    data,info,elapsed=capture(rate,ch=ch)
                    raw,path=ctx.samples('B7-%s-%d'%(name,rate),data,rate)
                    text=dev.decode_all().decode();ctx.save('B7-%s-%d-device.log'%(name,rate),text)
                    assert wants[name] in text,(name,rate,info,text)
                    bridge=ctx.bridge_run(raw,rate)
                    assert text==bridge['text'],'on-device and production C bridge text differ'
                    ctx.save('B7-%s-%d-C.json'%(name,rate),bridge)
                    ann='can=id' if name=='CAN' else None
                    oracle=sigrok(ctx,path,name,'B7-%s-%d-oracle'%(name,rate),ann)
                    if name=='CAN':
                        assert 'Identifier: 291' in oracle
                        payload=sigrok(ctx,path,name,'B7-CAN-%d-payload'%rate,'can=data')
                        assert oracle_bytes(payload,name)==[p['value'] for p in bridge['packets'][3] if p['flags']==0]
                    else:assert oracle_bytes(oracle,name)==EXPECTED[name],(name,oracle)
                    rows.append(dict(protocol=name,rate=rate,info=info,seconds=elapsed))
            ctx.save('B7-matrix.json',rows);return dict(cases=len(rows),all_expected_bytes=True)
        ctx.case('B7','Real four-bus decode rate matrix','4protocols x3rates;SPI100k at1MHz;CAN250k','expected complete bytes;device and sigrok agree',b7)
        def b8():
            cmd('set spi 100')
            data,info,_=capture(1000000,262144,mode=0)
            counts=[sum(bool((a^b)&(1<<ch)) for a,b in zip(data,data[1:])) for ch in range(8)]
            ctx.samples('B8-current-wiring',data,info['rate']);ctx.save('B8-channel-activity.json',counts)
            raise Blocked('Current wiring edge counts %s; distinct8-channel reference/common-edge skew source is not connected'%counts)
        ctx.case('B8','Independent8-channel mapping/crosstalk/skew','current bus wiring;reference all8not available','all8 driven by known independent waveforms;no undocumented rewiring',b8)
    finally:
        try:cmd('stress off');cmd('set spi 1000');cmd('burst on')
        finally:dev.close();gen.close()
