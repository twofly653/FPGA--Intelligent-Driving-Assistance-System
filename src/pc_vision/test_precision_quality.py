import unittest
import numpy as np
from track_video import cv2,tracker_arguments
from ultralytics.engine.results import Boxes
from precision_tracking import PrecisionTracker,AppearanceByteTracker
from quality_input import average565,decode565,display_detail

class QualityTests(unittest.TestCase):
    def test_bit_exact_channels_and_rounding(self):
        rng=np.random.default_rng(32);a=rng.integers(0,65536,(12,20),dtype=np.uint16);out=average565(a)
        for y in range(6):
            for x in range(10):
                values=[int(z) for z in a[2*y:2*y+2,2*x:2*x+2].ravel()];expected=0
                for shift,mask in ((11,31),(5,63),(0,31)):
                    expected|=((sum((v>>shift)&mask for v in values)+2)//4)<<shift
                self.assertEqual(int(out[y,x]),expected)
    def test_alias_reduction_on_checker(self):
        a=np.indices((80,100)).sum(axis=0)%2;a=np.where(a,65535,0).astype(np.uint16)
        filtered=decode565(average565(a));self.assertGreater(filtered.mean(),120);self.assertLess(filtered.mean(),140)
        self.assertEqual(int(decode565(a[::2,::2]).max()),0)
    def test_flat_preview_preserves_colour(self):
        im=np.full((20,20,3),100,np.uint8);self.assertTrue(np.array_equal(im,display_detail(im)))

class PrecisionTests(unittest.TestCase):
    def setUp(self):self.image=np.zeros((240,320,3),np.uint8);self.names={2:'car',3:'motorcycle',7:'truck'};self.tracker=PrecisionTracker()
    def update(self,rows,t):
        return self.tracker.update(Boxes(np.array(rows,np.float32).reshape(-1,6),self.image.shape[:2]),self.image,self.names,t,1)
    def test_no_ghost_after_loss_and_reassociation(self):
        row=[[40,60,80,100,.9,2]];tid=self.update(row,1000000)[0]['id'];self.update(row,1100000)
        self.assertEqual(self.update([],1300000),[])
        self.assertEqual(self.update(row,1500000)[0]['id'],tid)
    def test_fast_motion_not_smoothed_behind_observation(self):
        self.update([[40,60,90,110,.9,2]],1000000);self.update([[42,60,92,110,.9,2]],1100000)
        result=self.update([[60,60,110,110,.9,2]],1200000)
        self.assertTrue(result);self.assertEqual(result[0]['bbox_xyxy'],result[0]['measured_bbox_xyxy'])
    def test_duplicate_car_truck_hypothesis_does_not_create_second_id(self):
        row=[[40,60,100,120,.9,2],[41,60,99,119,.8,7]]
        first=self.update(row,1000000);second=self.update(row,1100000)
        self.assertEqual(len(first),1);self.assertEqual(len(second),1);self.assertEqual(first[0]['id'],second[0]['id'])
    def test_separate_overlapping_vehicles_are_kept(self):
        result=self.update([[40,60,100,120,.9,2],[62,60,122,120,.8,7]],1000000)
        self.assertEqual(len(result),2)
    def test_epoch_and_time_guards_are_preserved(self):
        self.update([[40,60,80,100,.9,2]],1000000)
        with self.assertRaises(ValueError):self.update([],900000)
        self.tracker.restart(2);self.assertEqual(self.tracker.box_history,{})
    def test_jitter_is_reduced_without_inventing_observations(self):
        raw=[];smooth=[]
        for i in range(30):
            x=50+(i%2)*1.5
            result=self.update([[x,60,x+40,100,.9,2]],1000000+i*33333)
            if result:raw.append(result[0]['measured_bbox_xyxy'][0]);smooth.append(result[0]['bbox_xyxy'][0])
        self.assertLess(np.abs(np.diff(smooth[5:])).mean(),np.abs(np.diff(raw[5:])).mean())
    def test_appearance_breaks_ambiguous_spatial_match(self):
        tr=AppearanceByteTracker(tracker_arguments(2),frame_rate=30)
        image=np.zeros((120,140,3),np.uint8);image[20:80,10:50]=(0,0,230);image[20:80,80:120]=(230,0,0)
        b=Boxes(np.array([[10,20,50,80,.9,2],[80,20,120,80,.9,2]],np.float32),image.shape[:2]).numpy()
        tracks=tr.init_track(b,image);detections=tr.init_track(b,image)
        for item in tracks+ detections:item._tlwh=np.array([30,20,40,60],np.float32)
        cost=tr.get_dists(tracks,detections)
        self.assertLess(cost[0,0],cost[0,1]);self.assertLess(cost[1,1],cost[1,0])
    def test_semantic_family_gate(self):
        tr=AppearanceByteTracker(tracker_arguments(2),frame_rate=30)
        def box(cls):return Boxes(np.array([[40,40,80,90,.9,cls]],np.float32),self.image.shape[:2]).numpy()
        a=tr.init_track(box(2),self.image);b=tr.init_track(box(3),self.image);c=tr.init_track(box(7),self.image)
        self.assertEqual(tr.get_dists(a,b)[0,0],1);self.assertLess(tr.get_dists(a,c)[0,0],.8)

if __name__=='__main__':unittest.main(verbosity=2)
