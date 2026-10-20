#!/usr/bin/env python3
"""Independent LAR2 v1 codec (standard library only)."""
import struct
import zlib
MAGIC=b'LAR2'
MAX_PAYLOAD=1024
MAX_SAMPLES=262144
RATES=(1000,10000,100000,250000,500000,1000000,5000000,25000000)
HELLO,GET_CAPS,CONFIG,ARM,ABORT=range(1,6)
ID,CAPS,ACK,INFO,SAMPLES,END,ERROR=0x81,0x82,0x83,0x90,0x91,0x92,0xff

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
