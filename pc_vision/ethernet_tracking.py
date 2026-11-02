"""Future Ethernet receiver. Running this explicitly contacts the specified board.
Offline development/tests must use the separate in-memory replay and loopback.
"""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import queue
import secrets
import socket
import struct
import sys
import threading
import time
import zlib
import ipaddress
from track_video import ROOT,YOLO,torch,cv2,np,annotate
from live_tracking import LiveTracker,HardwareSelection
from net_protocol import Header,pack,HELLO,Reassembler,result_packet
from network_preview import preview
from precision_tracking import PrecisionTracker
from latest_artifacts import LatestArtifacts
from quality_input import display_detail

def decode_rgb565(raw,width,height):
    a=np.frombuffer(raw,dtype='<u2').reshape(height,width)
    r=(a>>11)&31;g=(a>>5)&63;b=a&31
    return np.stack(((b<<3)|(b>>2),(g<<2)|(g>>4),(r<<3)|(r>>2)),axis=-1).astype(np.uint8)

def adopt_session(data,session):
    """Rewrite a zero-session datagram to the session this receiver announced.

    The flashed R15 candidate build echoes session 0 in every frame header even
    though it adopts the HELLO session internally, and net_protocol.unpack
    rejects session 0 as malformed. Only the session field and the CRC over the
    first 44 bytes are touched; every other byte and every other validation
    (magic, version, kind, layout, payload CRC) stays exactly as received.
    Datagrams that already carry a session are returned untouched.
    """
    if len(data)<48 or data[:4]!=b'DVN1' or data[8:12]!=b'\x00\x00\x00\x00':return data
    fixed=bytearray(data);fixed[8:12]=session.to_bytes(4,'big')
    fixed[44:48]=struct.pack('!I',zlib.crc32(bytes(fixed[48:]),zlib.crc32(bytes(fixed[:44]))))
    return bytes(fixed)

class Receiver:
    def __init__(self,board,bind,board_port=50010,local_port=50011):
        self.peer=(str(ipaddress.IPv4Address(board)),board_port)
        self.socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,2*1024*1024)
        if hasattr(socket,'SIO_UDP_CONNRESET'):self.socket.ioctl(socket.SIO_UDP_CONNRESET,False)
        self.socket.bind((str(ipaddress.IPv4Address(bind)),local_port));self.socket.settimeout(.05)
        self.session=secrets.randbits(32) or 1;self.assembly=Reassembler(self.session)
        self.latest=queue.Queue(maxsize=1);self.epoch=None;self.stop=threading.Event();self.dropped=0;self.last_error=None;self.rewritten=0
    def run(self):
        last_hello=0.
        while not self.stop.is_set():
            now=time.monotonic()
            if now-last_hello>=.4:
                try:self.socket.sendto(pack(Header(HELLO,self.session)),self.peer);self.last_error=None
                except OSError as error:self.last_error=str(error)
                last_hello=now
            try:data,peer=self.socket.recvfrom(2048)
            except socket.timeout:continue
            except OSError:
                if self.stop.is_set():break
                continue
            if peer!=self.peer:continue
            fixed=adopt_session(data,self.session)
            if fixed is not data:
                if not self.rewritten:print(f'[net] peer echoes session 0; adopting local session 0x{self.session:08x}',file=sys.stderr,flush=True)
                self.rewritten+=1
            result=self.assembly.accept(fixed)
            if result is None:continue
            h,raw=result;self.epoch=h.epoch
            try:self.latest.put_nowait((h,raw,time.monotonic()))
            except queue.Full:
                try:self.latest.get_nowait();self.dropped+=1
                except queue.Empty:pass
                self.latest.put_nowait((h,raw,time.monotonic()))
    def close(self):self.stop.set()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board',default='192.168.2.99');parser.add_argument('--bind',default='192.168.2.1')
    parser.add_argument('--weights',type=Path,default=ROOT/'models/yolov5su.pt')
    parser.add_argument('--imgsz',type=int,default=640);parser.add_argument('--show',action='store_true')
    # Vehicle classes by default (2=car,3=motorcycle,5=bus,7=truck). Indoor demo
    # needs person(0); pass e.g. --classes 0 2 3 5 7 to widen the filter.
    parser.add_argument('--classes',type=int,nargs='+',default=[2,3,5,7])
    parser.add_argument('--tracker',choices=['precision','baseline'],default='precision')
    parser.add_argument('--preview-detail',action='store_true',help='Mild luminance enhancement for preview only; detector sees raw RGB565')
    parser.add_argument('--output',type=Path,default=ROOT/'runs/ethernet_live')
    args=parser.parse_args()
    if not torch.cuda.is_available():raise RuntimeError('CUDA required; no silent CPU fallback')
    model=YOLO(str(args.weights));options=dict(device='0',half=True,imgsz=args.imgsz,conf=.1,iou=.6,classes=args.classes,verbose=False)
    for _ in range(3):model.predict(np.zeros((240,320,3),np.uint8),**options)
    args.output.mkdir(parents=True,exist_ok=True);tracker=PrecisionTracker() if args.tracker=='precision' else LiveTracker();selection=HardwareSelection()
    rx=Receiver(args.board,args.bind);thread=threading.Thread(target=rx.run,daemon=True);thread.start()
    artifacts=LatestArtifacts(args.output);last_report=0.
    try:
        while True:
            try:h,raw,received=rx.latest.get(timeout=.25)
            except queue.Empty:
                waiting=np.zeros((480,640,3),np.uint8)
                cv2.putText(waiting,'WAITING FOR CAMERA - NO VALID FRAME',(12,240),cv2.FONT_HERSHEY_SIMPLEX,.65,(255,255,255),1,cv2.LINE_AA)
                artifacts.submit(waiting,{'live':False,'source':'PC_ETHERNET','objects':[],'network_error':rx.last_error})
                if args.show:
                    cv2.imshow('DriveSense Ethernet GPU Tracking',waiting)
                    if cv2.waitKey(1)&255==27:break
                continue
            if time.monotonic()-received>.2:continue
            image=decode_rgb565(raw,h.width,h.height);start=time.perf_counter()
            if h.flags:
                detected=model.predict(image,**options)[0]
                try:objects=tracker.update(detected.boxes,image,model.names,h.capture_us,h.epoch)
                except ValueError:continue
            else:
                tracker.restart(h.epoch);objects=[];selection=HardwareSelection()
            elapsed=(time.perf_counter()-start)*1000
            if rx.epoch!=h.epoch or time.monotonic()-received>.25:continue
            chosen=selection.choose(objects,time.monotonic())
            try:rx.socket.sendto(result_packet(h,chosen,elapsed),rx.peer)
            except OSError as error:rx.last_error=str(error);continue
            state={'source':'PC_ETHERNET','live':True,'session':h.session,'epoch':h.epoch,'frame':h.frame,
                   'capture_us':h.capture_us,'source_width':h.width,'source_height':h.height,'proc_ms':elapsed,
                   'flags':h.flags,'zero_session_rewrites':rx.rewritten,
                   'objects':chosen,'all_objects':selection.visible_objects,'all_visible_count':len(objects),'queue_dropped':rx.dropped,
                   'tracker':args.tracker,'preview_dropped':artifacts.dropped,'preview_error':artifacts.error,
                   'hdmi_acknowledged':False,'note':'Hardware UART remains the authority for displayed HDMI boxes.'}
            view=preview(display_detail(image) if args.preview_detail else image,selection.visible_objects,h.capture_us/1e6)
            if time.monotonic()-last_report>=1.:
                last_report=time.monotonic()
                print(f'[live] frame={h.frame} epoch={h.epoch} flags={h.flags} detect={elapsed:.1f}ms '
                      f'raw={len(objects)} chosen={len(chosen)} dropped={rx.dropped} '
                      f'rewrites={rx.rewritten} session=0x{h.session:08x}',flush=True)
            artifacts.submit(view,state)
            if args.show:
                cv2.imshow('DriveSense Ethernet GPU Tracking',cv2.resize(view,(960,720)))
                if cv2.waitKey(1)&255==27:break
    except KeyboardInterrupt:pass
    finally:rx.close();thread.join(timeout=1);rx.socket.close();artifacts.close();cv2.destroyAllWindows()

if __name__=='__main__':main()
