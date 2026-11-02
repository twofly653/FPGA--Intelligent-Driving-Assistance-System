from pathlib import Path
import json,tempfile,time,unittest,threading
from unittest.mock import patch
from track_video import ROOT,np,cv2
from latest_artifacts import LatestArtifacts

class LatestArtifactTests(unittest.TestCase):
    def test_bounded_writer_finishes_on_newest_frame(self):
        cache=ROOT/'.cache/tests';cache.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=cache) as directory:
            original=cv2.imwrite;entered=threading.Event();release=threading.Event()
            def slow(path,image,*args):
                if not entered.is_set():entered.set();release.wait(1)
                return original(path,image,*args)
            with patch('latest_artifacts.cv2.imwrite',side_effect=slow):
                writer=LatestArtifacts(directory);image=np.zeros((20,20,3),np.uint8)
                writer.submit(image,{'frame':0,'live':True});self.assertTrue(entered.wait(1))
                for frame in range(1,20):writer.submit(image,{'frame':frame,'live':True})
                self.assertEqual(writer.queue.qsize(),1);self.assertGreater(writer.dropped,0)
                writer.submit(image,{'frame':20,'live':False,'objects':[]});release.set();writer.close()
            self.assertFalse(writer.thread.is_alive());self.assertIsNone(writer.error)
            self.assertEqual(json.loads((Path(directory)/'state.json').read_text())['frame'],20)
            self.assertFalse(json.loads((Path(directory)/'state.json').read_text())['live'])
    def test_write_failure_is_reported_and_worker_survives(self):
        cache=ROOT/'.cache/tests';cache.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=cache) as directory:
            with patch('latest_artifacts.cv2.imwrite',return_value=False):
                writer=LatestArtifacts(directory);writer.submit(np.zeros((8,8,3),np.uint8),{'frame':1});writer.close()
            self.assertIn('encode failed',writer.error)

if __name__=='__main__':unittest.main(verbosity=2)
