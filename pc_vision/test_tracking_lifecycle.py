"""Behavior checks for ID continuity/loss; no model or board required."""
import os
from pathlib import Path
ROOT = Path(__file__).resolve().parent
os.environ.setdefault('YOLO_CONFIG_DIR', str(ROOT / '.cache/ultralytics'))
os.environ['YOLO_AUTOINSTALL'] = 'false'
Path(os.environ['YOLO_CONFIG_DIR']).mkdir(parents=True, exist_ok=True)
import unittest
import numpy as np
import torch
from ultralytics.engine.results import Boxes
from ultralytics.trackers.byte_tracker import BYTETracker
from track_video import tracker_arguments, observed_tracks


class TrackLifecycle(unittest.TestCase):
    def setUp(self):
        self.image = np.zeros((240, 320, 3), dtype=np.uint8)
        self.tracker = BYTETracker(tracker_arguments(2.0), frame_rate=25)

    def feed(self, rows):
        boxes = Boxes(torch.tensor(rows, dtype=torch.float32).reshape(-1, 6), (240, 320))
        return observed_tracks(self.tracker, boxes, self.image, {2: 'car', 5: 'bus'})

    def test_same_moving_vehicle_retains_id(self):
        ids = []
        for x in range(30, 81, 2):
            objects = self.feed([[x, 70, x + 45, 105, 0.9, 2]])
            self.assertEqual(len(objects), 1)
            ids.append(objects[0]['id'])
        self.assertEqual(len(set(ids)), 1)

    def test_short_occlusion_recovers_id_without_ghost_boxes(self):
        before = None
        for _ in range(6):
            before = self.feed([[50, 70, 100, 110, 0.9, 2]])[0]['id']
        for _ in range(8):
            self.assertEqual(self.feed([]), [])
        after = self.feed([[52, 70, 102, 110, 0.9, 2]])
        self.assertEqual(after[0]['id'], before)

    def test_confirmed_stationary_vehicle_remains_tracked(self):
        ids = [self.feed([[60, 65, 105, 110, 0.9, 2]])[0]['id'] for _ in range(100)]
        self.assertEqual(len(set(ids)), 1)

    def test_long_loss_expires_identity(self):
        before = self.feed([[60, 65, 105, 110, 0.9, 2]])[0]['id']
        for _ in range(60):
            self.assertEqual(self.feed([]), [])
        self.feed([[60, 65, 105, 110, 0.9, 2]])
        after = self.feed([[60, 65, 105, 110, 0.9, 2]])
        self.assertNotEqual(after[0]['id'], before)

    def test_multiple_objects_have_distinct_persistent_ids(self):
        records = []
        for step in range(20):
            objects = self.feed([[20 + step, 30, 60 + step, 65, .9, 2],
                                 [180 - step, 140, 240 - step, 200, .9, 5]])
            self.assertEqual(len(objects), 2)
            records.append({o['class_name']: o['id'] for o in objects})
        self.assertTrue(all(row == records[0] for row in records))

    def test_low_confidence_observation_can_continue_confirmed_id(self):
        before = self.feed([[50, 60, 90, 100, .9, 2]])[0]['id']
        for _ in range(5):
            observed = self.feed([[51, 60, 91, 100, .2, 2]])
            self.assertEqual(observed[0]['id'], before)

    def test_empty_input_never_creates_a_track(self):
        for _ in range(70):
            self.assertEqual(self.feed([]), [])

    def test_lost_buffer_uses_video_frame_rate(self):
        for fps in (10, 22, 25, 30, 60):
            tracker = BYTETracker(tracker_arguments(2.0), frame_rate=fps)
            self.assertEqual(tracker.max_time_lost, fps * 2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
