"""Real libsigrok/PulseView and .sr interoperability evidence."""
import json
import os
import subprocess
import time
import zipfile
from qa_common import ROOT,require_run,Blocked
import demo


def register(ctx):
    demo.stop_previous_demo()
    def d1():
        p=ctx.run([ROOT/'scripts/run-sigrok-cli.sh','-d','rp2350-la:conn='+ctx.args.port,'--show'])
        ctx.save('D1-device-show.log',p.stdout+p.stderr);require_run(p)
        text=p.stdout+p.stderr
        assert 'RP2350' in text and 'Pico 2 / 2 W Logic Analyzer' in text
        for ch in range(8):assert 'CH%d'%ch in text
        for rate in ('1 kHz','10 kHz','100 kHz','250 kHz','500 kHz','1 MHz','5 MHz','25 MHz'):assert rate in text,rate
        return text
    ctx.case('D1','Device capability discovery','actual rp2350-la over USB CDC','correct name8channels/8rates',d1)
    def d2():
        out=ctx.out/'D2';out.mkdir()
        demo.setup_generator(ctx.args.esp32_port,out)
        path,ch=demo.capture_driver(ctx.args.port,out);ctx.d2_capture=path
        return dict(file=str(path.relative_to(ctx.out)),trigger='CH%d'%ch,rate=25000000,count=65536,
                    decoder_oracle='UART/I2C/SPI/CAN payload checked by demo.capture_driver')
    ctx.case('D2','Driver snapshot and PC decoder E2E','25MHz65536;edgefall;pre50%;realESP32','CRCverified and4bus expected data',d2)
    def d3():
        output=ctx.out/'D3-live'
        env=dict(os.environ,DISPLAY=os.environ.get('DISPLAY',':0'))
        p=subprocess.run(['bash','scripts/demo.sh','--port',ctx.args.port,'--esp32-port',ctx.args.esp32_port,
                          '--dwell','2','--detach-gui','--output',str(output)],cwd=ROOT,env=env,text=True,capture_output=True,timeout=210)
        ctx.save('D3-demo.log',p.stdout+p.stderr);require_run(p)
        summary=json.loads((output/'summary.json').read_text());assert summary['status']=='PASS',summary
        report=json.loads((output/'pulseview.json').read_text());demo.check_gui_report(report)
        pid=summary['pulseview_pid']
        windows=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(pid)],env=env,text=True,capture_output=True,timeout=5)
        assert windows.returncode==0,windows.stderr
        for win in windows.stdout.split():
            subprocess.run(['xdotool','windowactivate','--sync',win],env=env,capture_output=True,timeout=5)
        time.sleep(.5)
        shot=subprocess.run(['import','-window','root',str(ctx.out/'D3-root.png')],env=env,text=True,capture_output=True,timeout=10)
        assert shot.returncode==0,shot.stderr
        return dict(summary=summary['status'],gui=summary['gui'],config=report['device_config'],
                    annotations={d['name']:len(d['annotations']) for d in report['decoders']},
                    interaction='Native QSettings channel/baud restoration, actual GUI Run,xdotool focus,X11 screenshot;no CLI substitution')
    ctx.case('D3','PulseView live acquisition and visible annotations','4configured GUIdecoders;native settings;TFT2s/page','8waveforms;4visible expected decoder outputs;GUI+root PNG',d3)
    def d4():
        path=getattr(ctx,'d2_capture',None);assert path and path.exists(),'D2 capture missing'
        with zipfile.ZipFile(path) as z:
            metadata=z.read('metadata').decode();raw=b''.join(z.read(n) for n in sorted(z.namelist()) if n.startswith('logic-1-'))
        for ch in range(8):assert 'probe%d=CH%d'%(ch+1,ch) in metadata
        assert 'total probes=8' in metadata and 'samplerate=25 MHz' in metadata
        assert len(raw)==65536,len(raw)
        ctx.save('D4-metadata.txt',metadata)
        return dict(probes=8,unitsize_derived=1,binary_bytes=len(raw),rate=25000000)
    ctx.case('D4','Sigrok session metadata and binary layout','actual D2.sr;8channels','exact probe names/rate/65536one-byte samples',d4)
    def d5():
        # Independent concurrency regression for the GUI decoder-stack race.
        script='source scripts/env.sh\ncc -std=gnu11 -pthread tests/qa_srd_threads.c -o build/qa-v2/srd-threads $(pkg-config --cflags --libs libsigrokdecode)\ntimeout 30 build/qa-v2/srd-threads'
        p=ctx.run(['bash','-c',script],timeout=45);ctx.save('D5-session-race.log',p.stdout+p.stderr);require_run(p)
        assert 'sessions=40 failures=0' in p.stdout and 'decoder instance not found' not in p.stderr
        return p.stdout.strip()
    ctx.case('D5','Parallel decoder session lifecycle regression','4threads x10create/start/send/destroy cycles','0sessionlookupfailures;annotations generated',d5)
