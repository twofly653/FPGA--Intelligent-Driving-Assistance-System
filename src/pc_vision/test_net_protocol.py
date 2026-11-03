import random
import struct
import unittest
from net_protocol import *

class NetworkProtocol(unittest.TestCase):
    def setUp(self):self.raw=bytes(range(256))*600;self.packets=list(frame_packets(self.raw,123,4,9,1000000,320,240))
    def test_header_size_and_no_ip_fragmentation(self):
        self.assertEqual(HEADER.size,48);self.assertTrue(all(len(p)+28<=1500 for p in self.packets))
    def test_lossless_reordered_duplicates(self):
        r=Reassembler(123);packets=self.packets+self.packets[:8];random.Random(12).shuffle(packets);outputs=[]
        for p in packets:
            v=r.accept(p,1.)
            if v:outputs.append(v)
        self.assertEqual(len(outputs),1);self.assertEqual(outputs[0][1],self.raw)
    def test_missing_packet_never_completes(self):
        r=Reassembler(123)
        for p in self.packets[:-1]:self.assertIsNone(r.accept(p,1.))
        self.assertEqual(r.stats['complete'],0)
    def test_crc_corruption_rejected(self):
        r=Reassembler(123);p=bytearray(self.packets[0]);p[-1]^=1
        self.assertIsNone(r.accept(p,1.));self.assertEqual(r.stats['bad'],1)
    def test_wrong_session_rejected(self):
        r=Reassembler(999)
        for p in self.packets:self.assertIsNone(r.accept(p,1.))
    def test_new_frame_discards_old_partial(self):
        r=Reassembler(123);r.accept(self.packets[0],1.)
        new=list(frame_packets(self.raw,123,4,10,1100000,320,240));out=None
        for p in new:out=r.accept(p,1.05) or out
        self.assertEqual(out[0].frame,10);self.assertEqual(r.stats['dropped'],1)
        for p in self.packets:self.assertIsNone(r.accept(p,1.06))
    def test_timeout_cannot_resurrect_older_frame(self):
        r=Reassembler(123);r.accept(self.packets[0],1.)
        old=list(frame_packets(self.raw,123,3,99,100000,320,240))
        for p in old:self.assertIsNone(r.accept(p,1.4))
        self.assertEqual(r.stats['complete'],0)
    def test_inconsistent_metadata_rejected(self):
        r=Reassembler(123);r.accept(self.packets[0],1.)
        h,p=unpack(self.packets[1]);bad=Header(h.kind,h.session,h.epoch,h.frame,h.capture_us+1,h.width,h.height,h.flags,h.index,h.chunks,h.total)
        self.assertIsNone(r.accept(pack(bad,p),1.));self.assertEqual(r.stats['bad'],1)
    def test_conflicting_duplicate_invalidates_frame(self):
        r=Reassembler(123);r.accept(self.packets[0],1.);h,p=unpack(self.packets[0]);r.accept(pack(h,b'X'+p[1:]),1.)
        for packet in self.packets:self.assertIsNone(r.accept(packet,1.))
    def test_wraparound_frame_sequence(self):
        r=Reassembler(123)
        for frame in (0xfffffffe,0xffffffff,0,1):
            out=None
            for p in frame_packets(self.raw,123,4,frame,1000000+frame,320,240):out=r.accept(p,1.) or out
            self.assertIsNotNone(out)
    def test_result_packet(self):
        h,_=unpack(self.packets[0]);obj={'id':17,'bbox_xyxy':[10,20,29,39],'confidence':.901,'class_id':2}
        result,p=unpack(result_packet(h,[obj],27));self.assertEqual(result.kind,RESULT);self.assertEqual(struct.unpack_from('!HH',p),(27,1))
        self.assertEqual(BOX.unpack_from(p,4),(17,10,20,29,39,901,2,1))
    def test_result_duplicates_and_overflow_rejected(self):
        h,_=unpack(self.packets[0]);obj={'id':17,'bbox_xyxy':[10,20,29,39],'confidence':.9,'class_id':2}
        with self.assertRaises(ValueError):result_packet(h,[obj,obj],0)
        with self.assertRaises(ValueError):result_packet(h,[obj]*9,0)
    def test_garbage_and_truncation(self):
        for n in range(48):
            with self.assertRaises(ValueError):unpack(b'x'*n)
        with self.assertRaises(ValueError):unpack(self.packets[0]+b'x')

if __name__=='__main__':unittest.main(verbosity=2)
