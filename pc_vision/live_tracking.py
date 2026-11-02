"""Capture-time aware ByteTrack and stable hardware slot/ID mapping."""
from collections import Counter,deque
import numpy as np
from ultralytics.engine.results import Boxes
from ultralytics.trackers.basetrack import BaseTrack
from ultralytics.trackers.byte_tracker import BYTETracker,STrack
from track_video import tracker_arguments,observed_tracks

class TimedByteTracker(BYTETracker):
    prediction_steps=1
    def multi_predict(self,stracks):
        # Advance physical motion, not artificial missing observations. Calling
        # update(empty) here would delete every newly admitted tentative track.
        for _ in range(self.prediction_steps):STrack.multi_predict(stracks)

class LiveTracker:
    def __init__(self,lost_seconds=2.):
        self.lost_seconds=lost_seconds;self.tracker=None;self.epoch=None;self.last_us=None
        self.start_us=0;self.tick=0;self.histories={};self.reset_count=0
    def restart(self,epoch):
        counter=BaseTrack._count
        self.tracker=TimedByteTracker(tracker_arguments(self.lost_seconds),frame_rate=30)
        BaseTrack._count=counter
        self.epoch=epoch;self.last_us=None;self.tick=0;self.histories={};self.reset_count+=1
    def update(self,boxes,image,names,capture_us,epoch):
        if self.tracker is None or epoch!=self.epoch or (self.last_us is not None and capture_us-self.last_us>self.lost_seconds*1e6):self.restart(epoch)
        if self.last_us is not None and capture_us<=self.last_us:raise ValueError('non-increasing capture timestamp')
        if self.last_us is None:self.start_us=capture_us
        tick=round((capture_us-self.start_us)*30/1e6)
        steps=max(1,tick-self.tick)
        self.tracker.frame_id+=steps-1
        self.tracker.prediction_steps=steps
        result=observed_tracks(self.tracker,boxes,image,names)
        for obj in result:
            obj['raw_class_id']=obj['class_id']
            history=self.histories.setdefault(obj['id'],deque(maxlen=8))
            history.append((capture_us,obj['class_id'],obj['confidence']))
            votes=Counter()
            for stamp,cls,score in history:
                if capture_us-stamp<=700000:votes[cls]+=score
            cls=votes.most_common(1)[0][0];obj['class_id']=cls;obj['class_name']=names[cls]
        self.histories={k:v for k,v in self.histories.items() if capture_us-v[-1][0]<=self.lost_seconds*1e6}
        self.tick=tick;self.last_us=capture_us
        return result

class HardwareSelection:
    """Retain visible selected IDs; new high scores cannot evict them each frame."""
    def __init__(self):self.selected=[];self.mapping={};self.next_id=1;self.last_seen={};self.visible_objects=[]
    def choose(self,objects,now):
        visible={o['id']:o for o in objects}
        self.selected=[i for i in self.selected if i in visible]
        order=sorted(objects,key=lambda o:(o['bbox_xyxy'][3],o['confidence']),reverse=True)
        for obj in order:
            if len(self.selected)>=8:break
            if obj['id'] not in self.selected:self.selected.append(obj['id'])
        # A newly visible large near obstacle may replace a much smaller far
        # one; a small confidence change alone cannot cause slot churn.
        area=lambda o:max(0,o['bbox_xyxy'][2]-o['bbox_xyxy'][0])*max(0,o['bbox_xyxy'][3]-o['bbox_xyxy'][1])
        for obj in sorted(objects,key=area,reverse=True):
            if len(self.selected)==8 and obj['id'] not in self.selected:
                weakest=min(self.selected,key=lambda tid:area(visible[tid]))
                if area(obj)>3*area(visible[weakest]):self.selected[self.selected.index(weakest)]=obj['id']
        for obj in objects:self.last_seen[obj['id']]=now
        for tid in list(self.mapping):
            if now-self.last_seen.get(tid,0)>3.:self.mapping.pop(tid,None);self.last_seen.pop(tid,None)
        mapped={}
        for raw_id,obj in visible.items():
            if raw_id not in self.mapping:
                used=set(self.mapping.values())
                while self.next_id in used:self.next_id=self.next_id%65535+1
                self.mapping[raw_id]=self.next_id;self.next_id=self.next_id%65535+1
            item=dict(obj);item['raw_tracker_id']=raw_id;item['id']=self.mapping[raw_id];mapped[raw_id]=item
        self.visible_objects=list(mapped.values())
        return [mapped[i] for i in self.selected]
