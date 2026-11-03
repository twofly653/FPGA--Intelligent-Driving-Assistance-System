"""Ablate association changes on identical cached detections, offline only."""
import argparse,json,time
from pathlib import Path
from track_video import ROOT,YOLO,cv2,np
from live_tracking import LiveTracker,HardwareSelection
from precision_tracking import PrecisionTracker
from quality_input import camera_input
from evaluate_sparse import evaluate

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--labels',default='bilibili');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    a.output.mkdir(parents=True,exist_ok=False);cap=cv2.VideoCapture(str(a.source));fps=cap.get(cv2.CAP_PROP_FPS);frames=[]
    while True:
        ok,image=cap.read()
        if not ok:break
        frames.append(image)
    cap.release();model=YOLO(str(ROOT/'models/yolov5su.pt'));reports=[]
    for profile,hz in [('antialias320',10),('native640',15)]:
        variants={'baseline':LiveTracker(),'nms':PrecisionTracker(smooth=False),'nms_smooth':PrecisionTracker(),
                  'appearance006':PrecisionTracker(appearance_weight=.06),'appearance018':PrecisionTracker(appearance_weight=.18)}
        selectors={k:HardwareSelection() for k in variants};rows={k:[] for k in variants}
        for i in range(int(len(frames)/fps*hz)):
            t=i/hz;_,image=camera_input(frames[min(len(frames)-1,round(t*fps))],profile);h,w=image.shape[:2]
            received=not(i%23==7 or i%29==11)
            if received:prediction=model.predict(image,device=0,half=True,imgsz=640,conf=.1,iou=.6,classes=[2,3,5,7],verbose=False)[0]
            for name,tracker in variants.items():
                objects=tracker.update(prediction.boxes,image,model.names,1000000+round(t*1e6),1) if received else []
                selectors[name].choose(objects,t);rows[name].append({'time_s':t,'width':w,'height':h,'objects':selectors[name].visible_objects})
        for name in variants:
            out=a.output/f'{profile}_{name}';out.mkdir();(out/'summary.json').write_text(json.dumps({'rgb565_wire':True,'source':str(a.source)}))
            (out/'tracks.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows[name]));evaluate(out,a.labels)
            m=json.loads((out/'sparse_evaluation.json').read_text());reports.append({'profile':profile,'variant':name,'selected_boxes':m['selected_boxes'],'iou50':m['iou50_matches'],'mean_iou':m['mean_best_matched_iou'],'id_changes':m['sparse_id_changes']})
    (a.output/'comparison.json').write_text(json.dumps(reports,indent=2));print(json.dumps(reports,indent=2))
