"""Versioned, bounded UDP RGB565 and result protocol shared with bare-metal C."""
from dataclasses import dataclass
import struct
import zlib
import time

HEADER=struct.Struct('!4sBBHIIIQHHHHIHHI')
CHUNK=1344
HELLO,FRAME,RESULT,STATUS=1,2,3,4
BOX=struct.Struct('!IHHHHHBB')

@dataclass(frozen=True)
class Header:
    kind:int
    session:int
    epoch:int=0
    frame:int=0
    capture_us:int=0
    width:int=0
    height:int=0
    flags:int=1
    index:int=0
    chunks:int=0
    total:int=0

def pack(h,payload=b''):
    if len(payload)>CHUNK:raise ValueError('oversize datagram')
    prefix=HEADER.pack(b'DVN1',1,h.kind,h.flags,h.session,h.epoch,h.frame,h.capture_us,
                       h.width,h.height,h.index,h.chunks,h.total,len(payload),0,0)
    crc=zlib.crc32(payload,zlib.crc32(prefix[:44]))
    return prefix[:44]+struct.pack('!I',crc)+payload

def unpack(data):
    if len(data)<48:raise ValueError('short header')
    magic,ver,kind,flags,session,epoch,frame,ts,w,h,i,n,total,length,reserved,crc=HEADER.unpack_from(data)
    payload=data[48:]
    if magic!=b'DVN1' or ver!=1 or kind not in (1,2,3,4) or not session or flags not in (0,1) or reserved or len(payload)!=length or length>CHUNK:
        raise ValueError('invalid header')
    if zlib.crc32(payload,zlib.crc32(data[:44]))!=crc:raise ValueError('crc mismatch')
    if kind==FRAME:
        if not(0<w<=640 and 0<h<=480) or total!=w*h*2 or n!=(total+CHUNK-1)//CHUNK or not(0<=i<n) or length!=min(CHUNK,total-i*CHUNK):
            raise ValueError('invalid frame layout')
    elif i or n or total:raise ValueError('invalid control layout')
    return Header(kind,session,epoch,frame,ts,w,h,flags,i,n,total),payload

def frame_packets(raw,session,epoch,frame,capture_us,width,height,enabled=True):
    if len(raw)!=width*height*2:raise ValueError('wrong frame length')
    for i,start in enumerate(range(0,len(raw),CHUNK)):
        yield pack(Header(FRAME,session,epoch,frame,capture_us,width,height,int(enabled),i,
                          (len(raw)+CHUNK-1)//CHUNK,len(raw)),raw[start:start+CHUNK])

def result_packet(h,objects,proc_ms):
    if len(objects)>8:raise ValueError('hardware supports at most 8 boxes')
    out=bytearray(struct.pack('!HH',min(65535,max(0,round(proc_ms))),len(objects)))
    used=set()
    for obj in objects:
        tid=int(obj['id']);x1,y1,x2,y2=map(round,obj['bbox_xyxy'])
        if tid in used or not 0<tid<=65535 or not 0<=x1<x2<h.width or not 0<=y1<y2<h.height:raise ValueError('invalid result box')
        used.add(tid)
        out+=BOX.pack(tid,x1,y1,x2,y2,min(1000,max(0,round(obj['confidence']*1000))),obj['class_id'],1)
    return pack(Header(RESULT,h.session,h.epoch,h.frame,h.capture_us,h.width,h.height,h.flags),out)

def newer(a,b):return 0<((a-b)&0xffffffff)<0x80000000

class Reassembler:
    """One in-progress frame; old frames and incomplete frames never queue up."""
    def __init__(self,session,timeout=.15):
        self.session=session;self.timeout=timeout;self.current=None;self.latest=None
        self.buffer=None;self.seen=set();self.started=0.;self.stats={'bad':0,'old':0,'dropped':0,'complete':0}
    def accept(self,data,now=None):
        now=time.monotonic() if now is None else now
        try:h,p=unpack(data)
        except ValueError:self.stats['bad']+=1;return None
        if h.kind!=FRAME or h.session!=self.session:return None
        key=(h.epoch,h.frame)
        if self.latest is not None and not (newer(h.epoch,self.latest[0]) or (h.epoch==self.latest[0] and newer(h.frame,self.latest[1]))):
            self.stats['old']+=1;return None
        if self.current is not None:
            old=self.current
            if now-self.started>self.timeout:
                self.stats['dropped']+=1;self.latest=(old.epoch,old.frame);self.current=None
                if not(newer(h.epoch,self.latest[0]) or (h.epoch==self.latest[0] and newer(h.frame,self.latest[1]))):return None
            elif key!=(old.epoch,old.frame):
                if not(newer(h.epoch,old.epoch) or (h.epoch==old.epoch and newer(h.frame,old.frame))):self.stats['old']+=1;return None
                self.stats['dropped']+=1;self.current=None
        if self.current is None:
            self.current=h;self.buffer=bytearray(h.total);self.seen=set();self.started=now
        if (h.epoch,h.frame,h.capture_us,h.width,h.height,h.flags,h.chunks,h.total)!=(self.current.epoch,self.current.frame,self.current.capture_us,self.current.width,self.current.height,self.current.flags,self.current.chunks,self.current.total):
            self.stats['bad']+=1;return None
        if h.index in self.seen:
            if self.buffer[h.index*CHUNK:h.index*CHUNK+len(p)]!=p:
                self.stats['bad']+=1;self.latest=key;self.current=None
            return None
        self.buffer[h.index*CHUNK:h.index*CHUNK+len(p)]=p;self.seen.add(h.index)
        if len(self.seen)!=h.chunks:return None
        raw=bytes(self.buffer);first=self.current;self.latest=key;self.current=None;self.stats['complete']+=1
        return first,raw
