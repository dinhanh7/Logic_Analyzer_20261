#!/usr/bin/env python3
"""Independent LAR2 v1.2 codec (standard library only)."""
import struct
import zlib
MAGIC=b'LAR2'
MAX_PAYLOAD=1024
MAX_SAMPLES=262144
RATES=(1000,10000,100000,250000,500000,1000000,5000000,25000000)
HELLO,GET_CAPS,CONFIG,ARM,ABORT,ANALYZE=range(1,7)
ID,CAPS,ACK,INFO,SAMPLES,END,RESULT,ERROR=0x81,0x82,0x83,0x90,0x91,0x92,0x93,0xff

ANALYZE_DECODE=0
ANALYZE_SAVE=1
ANALYZE_LOAD=2
ANALYZE_SIG_SAVE=3
ANALYZE_SIG_COMPARE=4
ANALYZE_DIFF=5
ANALYZE_SESSION=6
ANALYZE_REPLAY=7
ANALYZE_PINPROBE=8
ANALYZE_SIZE=8

PROTO_UART=0x01
PROTO_I2C=0x02
PROTO_SPI=0x04
PROTO_CAN=0x08
PROTO_ALL=0x0f

RESULT_OK=0
RESULT_ERROR=1
RESULT_ABSENT=2

def frame(kind,seq,payload=b''):
    if len(payload)>MAX_PAYLOAD: raise ValueError('payload too large')
    head=struct.pack('<4sBBHI',MAGIC,1,kind,seq&0xffff,len(payload))+payload
    return head+struct.pack('<I',zlib.crc32(head))

class Parser:
    def __init__(self): self.buffer=bytearray()
    def feed(self,data):
        self.buffer.extend(data)
        result=[]
        while len(self.buffer)>=4:
            if self.buffer[:4]!=MAGIC:
                del self.buffer[0]; continue
            if len(self.buffer)<12: break
            _,version,kind,seq,n=struct.unpack_from('<4sBBHI',self.buffer)
            if version!=1 or n>MAX_PAYLOAD: raise ValueError('invalid header')
            if len(self.buffer)<n+16: break
            raw=bytes(self.buffer[:n+16]); del self.buffer[:n+16]
            if zlib.crc32(raw[:-4])!=struct.unpack_from('<I',raw,len(raw)-4)[0]: raise ValueError('CRC mismatch')
            result.append((kind,seq,raw[12:-4]))
        return result

def pattern(count,rate,mode='counter'):
    if mode=='counter': return bytes(i&255 for i in range(count))
    if mode=='square': return bytes(sum(((i//(25*(1<<ch)))&1)<<ch for ch in range(8)) for i in range(count))
    if mode=='uart':
        bits=[1]*20
        for b in b'RP2350 OK\r\n': bits.extend([0]+[(b>>j)&1 for j in range(8)]+[1])
        bits.extend([1]*20)
        return bytes((0xfe if (i//100)&1 else 0)|bits[(i*115200//rate)%len(bits)] for i in range(count))
    raise ValueError(mode)

TRIGGER_MANUAL,TRIGGER_EDGE,TRIGGER_PATTERN=0,1,2
EDGE_RISING,EDGE_FALLING=0,1

def config_payload(rate,count,mask,pre=0,trigger=None):
    """Build a CONFIG payload. Without a trigger this is the v1 layout that
    every host understands, with a trigger it is the v2 layout."""
    if trigger is None:
        return struct.pack('<QIIBBH',rate,count,0,mask,TRIGGER_MANUAL,0)
    mode,channel,edge,value,pattern_mask=trigger
    return struct.pack('<QIIBBBBBBHI',rate,count,pre,mask,mode,channel,edge,
                       value,pattern_mask,0,0)

def find_trigger(data,start,stop,trigger):
    """Mirror of the firmware matcher, used by the emulator and the tests.
    The trigger is only accepted from index start, which is where the
    pre-trigger buffer ends."""
    kind=trigger[0]
    if kind=='pattern':
        value=trigger[3]&trigger[4]; mask=trigger[4]
        for i in range(start,stop):
            if (data[i]&mask)==value: return i
        return None
    if kind=='edge':
        channel,edge=trigger[1],trigger[2]; bit=1<<channel
        for i in range(start,stop):
            cur=data[i]&bit; prev=(data[i-1]&bit) if i else 0
            if edge==EDGE_RISING and cur and not prev: return i
            if edge==EDGE_FALLING and not cur and prev: return i
        return None
    return None

def analyze_payload(op, arg=0, arg32=0):
    """Build an ANALYZE payload (8 bytes): op, arg u16, arg32 u32, reserved."""
    return struct.pack('<BHI', op, arg, arg32) + b'\x00'
