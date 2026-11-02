"""Capture-time tracking + vehicle de-duplication + light jitter damping.

Colour-appearance matching is an experimental option, disabled by default after
cross-video regression. No synthetic observations or predicted ghost boxes.
"""
import math
import numpy as np
from track_video import cv2,tracker_arguments
from live_tracking import LiveTracker,TimedByteTracker
from ultralytics.trackers.byte_tracker import STrack
from ultralytics.trackers.basetrack import BaseTrack
from ultralytics.engine.results import Boxes

def vehicle_family_nms(boxes,threshold=.8):
    """Remove near-identical car/bus/truck hypotheses before track creation.

    The detector's default class-wise NMS can leave two classes for one vehicle.
    Keep ordinary overlap and motorcycles; suppress only IoU above 0.8 within
    the same vehicle family. This is not a semantic instance merge.
    """
    data=boxes.cpu().numpy().data
    keep=[]
    for index in np.argsort(-data[:,4],kind='stable'):
        a=data[index];duplicate=False
        for previous in keep:
            b=data[previous]
            if int(a[5]) not in (2,5,7) or int(b[5]) not in (2,5,7):continue
            inter=np.maximum(0,np.minimum(a[2:4],b[2:4])-np.maximum(a[:2],b[:2])).prod()
            union=np.maximum(0,a[2:4]-a[:2]).prod()+np.maximum(0,b[2:4]-b[:2]).prod()-inter
            if union>0 and inter/union>threshold:duplicate=True;break
        if not duplicate:keep.append(index)
    return Boxes(data[keep],boxes.orig_shape)

def appearance(image,xyxy):
    if image is None:return None
    h,w=image.shape[:2];x1,y1,x2,y2=map(float,xyxy)
    dx=(x2-x1)*.15;dy=(y2-y1)*.15
    x1,y1=max(0,int(x1+dx)),max(0,int(y1+dy))
    x2,y2=min(w,int(x2-dx)),min(h,int(y2-dy))
    if x2-x1<4 or y2-y1<4:return None
    patch=cv2.resize(image[y1:y2,x1:x2],(24,24),interpolation=cv2.INTER_AREA)
    hsv=cv2.cvtColor(patch,cv2.COLOR_BGR2HSV)
    # Saturation distinguishes white/grey vehicles from coloured backgrounds.
    hist=cv2.calcHist([hsv],[0,1],None,[12,4],[0,180,0,256]).ravel()
    return hist/max(float(hist.sum()),1.)

class AppearanceTrack(STrack):
    def __init__(self,xywh,score,cls,descriptor):
        super().__init__(xywh,score,cls);self.descriptor=descriptor
    def blend(self,new):
        if new.descriptor is None:return
        if self.descriptor is None:self.descriptor=new.descriptor.copy()
        else:self.descriptor=.85*self.descriptor+.15*new.descriptor
    def update(self,new_track,frame_id):
        super().update(new_track,frame_id);self.blend(new_track)
    def re_activate(self,new_track,frame_id,new_id=False):
        super().re_activate(new_track,frame_id,new_id);self.blend(new_track)

class AppearanceByteTracker(TimedByteTracker):
    appearance_weight=.18
    def init_track(self,results,img=None):
        if not len(results):return []
        return [AppearanceTrack(np.r_[xywh,index],score,cls,appearance(img,xyxy) if self.appearance_weight else None)
                for index,(xywh,xyxy,score,cls) in enumerate(zip(results.xywh,results.xyxy,results.conf,results.cls))]
    def get_dists(self,tracks,detections):
        cost=super().get_dists(tracks,detections)
        if not self.appearance_weight:return cost
        for i,a in enumerate(tracks):
            for j,b in enumerate(detections):
                aa,bb=a.xyxy,b.xyxy
                aw,ah=max(1.,aa[2]-aa[0]),max(1.,aa[3]-aa[1])
                bw,bh=max(1.,bb[2]-bb[0]),max(1.,bb[3]-bb[1])
                displacement=np.linalg.norm((aa[:2]+aa[2:]-bb[:2]-bb[2:])/2)
                # Car/bus/truck may flicker within the vehicle family; motorcycle
                # is separate. Distant or implausibly resized targets cannot match.
                impossible=(int(a.cls)==3)!=(int(b.cls)==3)
                impossible|=displacement>1.5*max(math.hypot(aw,ah),math.hypot(bw,bh))
                impossible|=max(aw*ah/(bw*bh),bw*bh/(aw*ah))>6
                if impossible:cost[i,j]=1.;continue
                if a.descriptor is not None and b.descriptor is not None:
                    difference=math.sqrt(max(0.,1.-float(np.sqrt(a.descriptor*b.descriptor).sum())))
                    cost[i,j]=(1-self.appearance_weight)*cost[i,j]+self.appearance_weight*difference
                    if difference>.88 and cost[i,j]>.6:cost[i,j]=1.
        return cost

class PrecisionTracker(LiveTracker):
    def __init__(self,lost_seconds=2.,appearance_weight=0.,deduplicate=True,smooth=True):
        self.appearance_weight=appearance_weight;self.deduplicate=deduplicate;self.smooth=smooth
        super().__init__(lost_seconds);self.box_history={}
    def restart(self,epoch):
        counter=BaseTrack._count
        self.tracker=AppearanceByteTracker(tracker_arguments(self.lost_seconds),frame_rate=30)
        self.tracker.appearance_weight=self.appearance_weight
        BaseTrack._count=counter
        self.epoch=epoch;self.last_us=None;self.tick=0;self.histories={};self.reset_count+=1;self.box_history={}
    def update(self,boxes,image,names,capture_us,epoch):
        if self.deduplicate:boxes=vehicle_family_nms(boxes)
        objects=super().update(boxes,image,names,capture_us,epoch)
        for obj in objects:
            current=np.array(obj['bbox_xyxy'],float);previous=self.box_history.get(obj['id'])
            obj['measured_bbox_xyxy']=list(obj['bbox_xyxy'])
            if previous is not None:
                last,stamp=previous;dt=(capture_us-stamp)/1e6
                shift=np.linalg.norm((current[:2]+current[2:]-last[:2]-last[2:])/2)
                size=max(1.,np.linalg.norm(current[2:]-current[:2]))
                # Only damp sub-pixel/low-speed jitter; quick movement/reappearance
                # snaps to the latest observation instead of trailing the vehicle.
                if self.smooth and 0<dt<=.18 and shift<size*.08:
                    alpha=max(.55,1-math.exp(-dt/.035));current=alpha*current+(1-alpha)*last
            self.box_history[obj['id']]=(current,capture_us)
            obj['bbox_xyxy']=[round(float(x),2) for x in current]
        self.box_history={k:v for k,v in self.box_history.items() if capture_us-v[1]<self.lost_seconds*1e6}
        return objects
