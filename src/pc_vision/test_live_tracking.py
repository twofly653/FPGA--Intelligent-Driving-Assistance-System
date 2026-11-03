import unittest
from track_video import np
from ultralytics.engine.results import Boxes
from live_tracking import LiveTracker,HardwareSelection

class LiveTrackingContracts(unittest.TestCase):
    def setUp(self):self.tracker=LiveTracker();self.image=np.zeros((240,320,3),np.uint8);self.names={2:'car',7:'truck'}
    def update(self,t,x=50,score=.9,cls=2,epoch=1):
        b=Boxes(np.array([[x,50,x+40,90,score,cls]],np.float32),self.image.shape[:2])
        return self.tracker.update(b,self.image,self.names,t,epoch)
    def test_variable_capture_intervals_preserve_stationary_id(self):
        values=[self.update(t)[0]['id'] for t in (1000000,1100000,1300000,1400000,1800000,1900000)]
        self.assertEqual(len(set(values)),1)
    def test_dropped_intervals_use_capture_time(self):
        for t in range(1000000,2000000,100000):self.update(t)
        self.update(2500000)
        self.assertGreater(self.tracker.tracker.frame_id,40)
    def test_old_time_rejected(self):
        self.update(1000000)
        with self.assertRaises(ValueError):self.update(999999)
    def test_new_target_can_confirm_after_stream_has_started(self):
        empty=Boxes(np.empty((0,6),np.float32),self.image.shape[:2])
        for t in (1000000,1100000,1200000):self.tracker.update(empty,self.image,self.names,t,1)
        self.update(1300000)
        detected=self.update(1400000)
        self.assertEqual(len(detected),1)
    def test_long_gap_resets_track(self):
        old=self.update(1000000)[0]['id'];self.update(4000000)
        new=self.update(4100000)[0]['id'];self.assertNotEqual(old,new)
    def test_label_vote_reduces_single_frame_flicker(self):
        for t in range(1000000,1500000,100000):self.update(t,cls=2)
        self.assertEqual(self.update(1500000,cls=7)[0]['class_id'],2)
    def test_epoch_resets_old_history(self):
        self.update(1000000);self.update(1100000,epoch=2);self.assertEqual(self.tracker.epoch,2);self.assertEqual(self.tracker.reset_count,2)
    def test_stable_eight_slots_and_bounded_ids(self):
        selector=HardwareSelection()
        def obj(i,y=100):return {'id':i,'bbox_xyxy':[0,y,10,y+10],'confidence':.8,'class_id':2}
        first=selector.choose([obj(i) for i in range(1,9)],1)
        second=selector.choose([obj(i) for i in range(1,9)]+[obj(999,200)],1.1)
        self.assertEqual([q['raw_tracker_id'] for q in first],[q['raw_tracker_id'] for q in second])
        selector.next_id=65535
        third=selector.choose([obj(100000),obj(100001)],5)
        self.assertTrue(all(0<q['id']<=65535 for q in third));self.assertEqual(len({q['id'] for q in third}),2)
    def test_pc_keeps_all_ids_but_hardware_has_eight_slots(self):
        selector=HardwareSelection()
        objects=[{'id':i,'bbox_xyxy':[0,20,10,30],'confidence':.8,'class_id':2} for i in range(1,13)]
        chosen=selector.choose(objects,1)
        self.assertEqual(len(chosen),8);self.assertEqual(len(selector.visible_objects),12)
        self.assertEqual(len({q['id'] for q in selector.visible_objects}),12)
    def test_near_large_target_can_replace_small_far_slot(self):
        selector=HardwareSelection()
        small=[{'id':i,'bbox_xyxy':[i*12,20,i*12+10,30],'confidence':.8,'class_id':2} for i in range(1,9)]
        selector.choose(small,1)
        chosen=selector.choose(small+[{'id':99,'bbox_xyxy':[50,50,150,150],'confidence':.7,'class_id':2}],1.1)
        self.assertIn(99,[q['raw_tracker_id'] for q in chosen]);self.assertEqual(len(chosen),8)

if __name__=='__main__':unittest.main(verbosity=2)
