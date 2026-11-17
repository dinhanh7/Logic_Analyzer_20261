"""New differential, adversarial and timing tests independent of old fixtures."""
import csv
import json
import re
from pathlib import Path
from qa_common import ROOT,require_run
from qa_signals import Wave

STACK={'UART':'uart:rx=CH0:baudrate=115200','I2C':'i2c:scl=CH1:sda=CH2',
       'SPI':'spi:clk=CH4:mosi=CH5:cs=CH3','CAN':'can:can_rx=CH7:nominal_bitrate=250000'}
ANNOT={'UART':'uart=rx-data','I2C':'i2c=data-write','SPI':'spi=mosi-data','CAN':'can=data'}
EXPECTED={'UART':list(b'LA1234\n'),'I2C':[1,0x5a],'SPI':list(bytes.fromhex('DE AD BE EF 00 01 02 03')),'CAN':[0xde,0xad,0xbe,0xef]}


def sigrok(ctx,path,name,id,annotation=None):
    p=ctx.run([ROOT/'scripts/run-sigrok-cli.sh','-i',path,'-P',STACK[name],'-A',annotation or ANNOT[name]],timeout=90)
    ctx.save(id+'.log',p.stdout+'\n'+p.stderr);require_run(p)
    lines=[re.sub(r'^\S+-1:\s*','',line).strip() for line in p.stdout.splitlines()]
    return '\n'.join(lines)


def oracle_bytes(text,name):
    if name=='I2C':return [int(v,16) for v in re.findall(r'Data write: ([0-9A-Fa-f]{2})',text)]
    if name=='CAN':
        # CAN annotates each payload byte as "Data: 222 (0xde)".
        return [int(v,16) for v in re.findall(r'0x([0-9A-Fa-f]{2})\b',text)]
    return [int(v,16) for v in re.findall(r'\b[0-9A-Fa-f]{2}\b',text)]


def register(ctx):
    state={}
    def build():
        ctx.bridge.parent.mkdir(parents=True,exist_ok=True)
        p=ctx.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                   '-Ifirmware','-Iprotocol','tests/qa_decoder_bridge.c','firmware/decoders.c',
                   'firmware/autodetect.c','-o',ctx.bridge])
        ctx.save('A0-build.log',p.stdout+p.stderr);require_run(p)
        return 'ASan/UBSan bridge built from production decoder/autodetect sources'
    ctx.case('A0','Compile the production C oracle bridge','offline compiler','build succeeds with sanitizers',build)
    def a1():
        w=Wave();w.uart();w.i2c();w.spi();w.can()
        raw,path=ctx.samples('A1-four-protocols',w.data,w.rate)
        state.update(raw=raw,path=path,rate=w.rate,oracle={})
        for name in STACK:
            text=sigrok(ctx,path,name,'A1-'+name)
            got=oracle_bytes(text,name);state['oracle'][name]=got
            assert got==EXPECTED[name],(name,got,text)
        address=sigrok(ctx,path,'I2C','A1-I2C-address','i2c=address-write')
        canid=sigrok(ctx,path,'CAN','A1-CAN-id','can=id')
        dlc=sigrok(ctx,path,'CAN','A1-CAN-dlc','can=dlc')
        assert 'Address write: 68' in address and 'Identifier: 291' in canid and 'Data length code: 4' in dlc
        ctx.save('A1-oracle.json',state['oracle'])
        return state['oracle']
    ctx.case('A1','Independent .sr four-bus oracle','synthetic 25MHz;UART115200,I2C100k,SPI100k,CAN250k','all bytes/address/ID/DLC match',a1)
    def a2():
        assert 'raw' in state,'A1 fixture was not produced'
        data=ctx.bridge_run(state['raw']);ctx.save('A2-production-decoder.json',data)
        got={}
        for i,name in enumerate(STACK):
            got[name]=[p['value'] for p in data['packets'][i] if p['flags']==0]
            assert got[name]==state['oracle'][name],(name,got[name],state['oracle'][name])
        can=data['packets'][3]
        assert [p['value'] for p in can[:3]]==[1,0x23,4]
        i2c=data['packets'][1];assert i2c[0]['value']==0xd0 and all(p['flags']==2 for p in i2c[1::2])
        return dict(same_file=str(state['path'].name),firmware=got,oracle=state['oracle'])
    ctx.case('A2','Byte-level C versus sigrok differential','exact A1 byte stream;real firmware C','100% fields/bytes identical',a2)
    def a3():
        vectors={'empty':b'','zero':bytes(32768),'ones':bytes([255])*32768}
        for name,options in [('missing-stop',dict(stop=False)),('bad-even-parity',dict(parity=True))]:
            w=Wave();w.uart(**options);vectors[name]=w.data
        w=Wave();w.spi(irregular=True);vectors['irregular-spi-clock']=w.data
        w=Wave();w.can();vectors['can-wrong-bitrate']=w.data
        for seed in range(24):vectors['seeded-noise-%02d'%seed]=Wave(count=4096).noise(seed).data
        w=Wave()
        for ch in range(8):
            for p in range(100+ch,4000,137):w.level(ch,p,p+1+(p%2),0)
        vectors['one-two-sample-glitches']=w.data
        details=[]
        for name,data in vectors.items():
            raw=ctx.out/('A3-'+name+'.raw');raw.write_bytes(data)
            options={'can':500000} if name=='can-wrong-bitrate' else {}
            a=ctx.bridge_run(raw,**options);b=ctx.bridge_run(raw,**options)
            assert a==b,name
            details.append(dict(vector=name,bytes=len(data),deterministic=True,packets=[len(p) for p in a['packets']]))
            if name=='irregular-spi-clock':assert [p['value'] for p in a['packets'][2]]==EXPECTED['SPI']
        ctx.save('A3-adversarial.json',details)
        return dict(vectors=len(vectors),runs=2*len(vectors),sanitizer_errors=0,
                    parity_validation='unsupported: firmware is 8N1; this case verifies safety only')
    ctx.case('A3','Decoder adversarial safety/determinism','34 bounded vectors;ASan/UBSan;6s/process','0crash,0hang;identical repeat outputs',a3)
    def a4():
        rows=[]
        for rate in (1000000,5000000,25000000):
            for baud in (9600,57600,115200,230400):
                w=Wave(rate,min(262144,max(32768,round(rate/baud*140))))
                w.uart(baud=baud,payload=b'UUUUUUUUUUUU',start=100)
                raw,_=ctx.samples('A4-%d-%d'%(rate,baud),w.data,rate)
                got=ctx.bridge_run(raw,rate,uart=0)
                found=re.search(r'UART (\d+) 8N1:',got['text']);assert found,got['text']
                measured=rate/int(found[1]);theory=rate/baud;error=(measured/theory-1)*100
                rows.append(dict(rate=rate,baud=baud,expected_samples=theory,measured_samples=measured,error_percent=error))
                assert abs(error)<2,rows[-1]
        with (ctx.out/'A4-bit-width.csv').open('w',newline='') as f:
            wr=csv.DictWriter(f,fieldnames=list(rows[0]));wr.writeheader();wr.writerows(rows)
        return dict(cases=len(rows),max_error_percent=max(abs(r['error_percent']) for r in rows))
    ctx.case('A4','Autobaud Q16 width accuracy','3rates x4bauds;fractional widths;independent alternating UART','absolute error<2%',a4)
    def a5():
        cases=[]
        for name in ('UART','I2C','SPI','CAN'):
            for moved in (False,True):
                w=Wave()
                if name=='UART':w.uart(ch=6 if moved else 0);mapping=[6] if moved else [0];slots=[0]
                elif name=='I2C':w.i2c(scl=5 if moved else 1,sda=6 if moved else 2);mapping=[5,6] if moved else [1,2];slots=[1,2]
                elif name=='SPI':w.spi(cs=0 if moved else 3,sck=1 if moved else 4,mosi=2 if moved else 5);mapping=[0,1,2] if moved else [3,4,5];slots=[3,4,5]
                else:w.can(ch=3 if moved else 7);mapping=[3] if moved else [7];slots=[7]
                raw,_=ctx.samples('A5-%s-%s'%(name,'moved' if moved else 'fixed'),w.data,w.rate)
                d=ctx.bridge_run(raw)['detect'];expected=1<<list(STACK).index(name)
                assert d['proto']==expected and [d['map'][i] for i in slots]==mapping,(name,moved,d,mapping)
                cases.append(dict(protocol=name,moved=moved,detected=d))
        for seed in range(8):
            w=Wave(count=32768).noise(seed+100);raw,_=ctx.samples('A5-noise-%d'%seed,w.data,w.rate)
            d=ctx.bridge_run(raw)['detect'];assert d['proto']==0,(seed,d)
        ctx.save('A5-maps.json',cases)
        return dict(bus_cases=len(cases),noise_cases=8,wrong_guesses=0)
    ctx.case('A5','AUTO moved-channel and noise rejection','4protocols x fixed/moved plus8noise seeds','correct protocol and map;noise=>no bus',a5)
