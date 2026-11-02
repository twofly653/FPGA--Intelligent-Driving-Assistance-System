"""Optional disk preview producer: bounded newest-only queue, outside inference."""
from pathlib import Path
import json,queue,threading,copy
from track_video import cv2

class LatestArtifacts:
    def __init__(self,directory):
        self.directory=Path(directory);self.directory.mkdir(parents=True,exist_ok=True)
        self.queue=queue.Queue(maxsize=1);self.stop=threading.Event();self.error=None;self.dropped=0
        self.thread=threading.Thread(target=self.run,name='preview-writer',daemon=True);self.thread.start()
    def submit(self,image,state):
        item=(image.copy(),copy.deepcopy(state))
        try:self.queue.put_nowait(item)
        except queue.Full:
            try:self.queue.get_nowait();self.dropped+=1
            except queue.Empty:pass
            self.queue.put_nowait(item)
    def run(self):
        while not self.stop.is_set() or not self.queue.empty():
            try:image,state=self.queue.get(timeout=.05)
            except queue.Empty:continue
            try:
                photo=self.directory/'latest.tmp.jpg'
                if not cv2.imwrite(str(photo),image,[cv2.IMWRITE_JPEG_QUALITY,94]):raise OSError('preview encode failed')
                photo.replace(self.directory/'latest.jpg')
                metadata=self.directory/'state.tmp';metadata.write_text(json.dumps(state),encoding='utf-8');metadata.replace(self.directory/'state.json')
                self.error=None
            except (OSError,cv2.error) as error:self.error=str(error)
    def close(self):
        self.stop.set();self.thread.join(timeout=2)
