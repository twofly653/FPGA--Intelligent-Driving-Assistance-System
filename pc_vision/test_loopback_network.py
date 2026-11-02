"""Only loopback sockets; never connects PYNQ or uses its configured IP."""
import socket,threading,time,unittest
from ethernet_tracking import Receiver
from net_protocol import *

class LocalUdpRoundtrip(unittest.TestCase):
    def test_frame_and_results_roundtrip(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as board:
            board.bind(('127.0.0.1',0));board.settimeout(1)
            rx=Receiver('127.0.0.1','127.0.0.1',board.getsockname()[1],0)
            worker=threading.Thread(target=rx.run);worker.start()
            try:
                hello,peer=board.recvfrom(2048);h,_=unpack(hello);self.assertEqual(h.kind,HELLO)
                raw=bytes(64*48*2)
                for p in frame_packets(raw,h.session,3,19,1000000,64,48):board.sendto(p,peer)
                frame,pixels,received=rx.latest.get(timeout=1)
                self.assertEqual(pixels,raw);self.assertEqual(frame.frame,19)
                obj={'id':17,'bbox_xyxy':[10,10,20,20],'confidence':.9,'class_id':2}
                rx.socket.sendto(result_packet(frame,[obj],22),rx.peer)
                data,source=board.recvfrom(2048);result,body=unpack(data)
                self.assertEqual(result.kind,RESULT);self.assertEqual(result.capture_us,1000000)
                self.assertEqual(BOX.unpack_from(body,4)[0],17)
            finally:rx.close();worker.join(timeout=2);rx.socket.close()
    def test_latest_only_and_wrong_sender(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as board,socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as wrong:
            board.bind(('127.0.0.1',0));board.settimeout(1)
            rx=Receiver('127.0.0.1','127.0.0.1',board.getsockname()[1],0)
            worker=threading.Thread(target=rx.run);worker.start()
            try:
                hello,peer=board.recvfrom(2048);h,_=unpack(hello)
                for p in frame_packets(bytes(8),h.session,3,9,1000000,2,2):wrong.sendto(p,peer)
                for i in range(1,5):
                    for p in frame_packets(bytes(8),h.session,3,i,1000000+i,2,2):board.sendto(p,peer)
                end=time.monotonic()+1
                while rx.assembly.stats['complete']<4 and time.monotonic()<end:time.sleep(.005)
                frame,_,_=rx.latest.get(timeout=1)
                self.assertEqual(rx.assembly.stats['complete'],4);self.assertEqual(frame.frame,4);self.assertEqual(rx.dropped,3)
            finally:rx.close();worker.join(timeout=2);rx.socket.close()

if __name__=='__main__':unittest.main(verbosity=2)
