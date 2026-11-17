"""Independent, deterministic wire-level vectors for QA; no firmware builders."""
import random

class Wave:
    def __init__(self, rate=25000000, count=32768):
        self.rate=rate;self.data=bytearray([255])*count
    def level(self,ch,a,b,level):
        a=max(0,round(a));b=min(len(self.data),round(b))
        if b>a:
            table=bytes((i|(1<<ch)) if level else (i&~(1<<ch)) for i in range(256))
            self.data[a:b]=self.data[a:b].translate(table)
    def uart(self,ch=0,baud=115200,payload=b'LA1234\n',start=200,stop=True,parity=False):
        width=self.rate/baud;p=float(start)
        for byte in payload:
            bits=[0]+[(byte>>b)&1 for b in range(8)]
            if parity:bits += [1-(sum(bits[1:])&1)] # deliberately incorrect even parity
            bits += [1 if stop else 0]
            for bit in bits:self.level(ch,p,p+width,bit);p+=width
        if not stop:self.level(ch,p,len(self.data),0)
        return p
    def i2c(self,scl=1,sda=2,khz=100,start=300,payload=(0xd0,1,0x5a)):
        h=self.rate/(khz*2000);p=float(start)
        self.level(sda,p,p+h,0);p+=h
        for byte in payload:
            for bit in [(byte>>b)&1 for b in range(7,-1,-1)]+[0]:
                self.level(scl,p,p+h,0);self.level(scl,p+h,p+2*h,1)
                self.level(sda,p,p+2*h,bit);p+=2*h
        self.level(scl,p,p+h,0);self.level(sda,p,p+2*h,0)
        self.level(scl,p+h,p+2*h,1)
        return p+2*h
    def spi(self,cs=3,sck=4,mosi=5,khz=100,start=450,payload=bytes.fromhex('DE AD BE EF 00 01 02 03'),irregular=False):
        self.level(sck,0,len(self.data),0);self.level(mosi,0,len(self.data),0)
        p=float(start);begin=p;h=self.rate/(khz*2000)
        for n,bit in enumerate((byte>>b)&1 for byte in payload for b in range(7,-1,-1)):
            half=h*(.7+(n%5)*.15) if irregular else h
            self.level(sck,p,p+half,0);self.level(sck,p+half,p+2*half,1)
            self.level(mosi,p,p+2*half,bit);p+=2*half
        self.level(cs,begin,p,0)
        return p
    def can(self,ch=7,baud=250000,identifier=0x123,payload=bytes.fromhex('DE AD BE EF'),start=600):
        bits=[0]+[(identifier>>b)&1 for b in range(10,-1,-1)]+[0,0,0]
        bits += [(len(payload)>>b)&1 for b in range(3,-1,-1)]
        bits += [(v>>b)&1 for v in payload for b in range(7,-1,-1)]
        crc=0
        for bit in bits:
            top=((crc>>14)&1)^bit;crc=(crc<<1)&0x7fff
            if top:crc^=0x4599
        bits += [(crc>>b)&1 for b in range(14,-1,-1)]
        stuffed=[];last=None;run=0
        for bit in bits:
            stuffed.append(bit);run=run+1 if bit==last else 1;last=bit
            if run==5:stuffed.append(1-bit);last=1-bit;run=1
        stuffed += [1,0,1]+[1]*10 # delimiter, ACK, delimiter, EOF and intermission
        p=float(start);width=self.rate/baud
        for bit in stuffed:self.level(ch,p,p+width,bit);p+=width
        return p
    def noise(self,seed=2350):
        rng=random.Random(seed)
        self.data[:]=bytes(rng.randrange(256) for _ in self.data)
        return self
