"""Scoped sparse box coverage, not exhaustive accuracy or MOT benchmark."""
from pathlib import Path
import argparse, hashlib, json
H=Path(__file__).resolve().parent
LABELS=json.loads((H/'sparse_labels.json').read_text())['frames']
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--historical',action='store_true',help='Include saved exploratory variants; not current-source reproduction.')
args=parser.parse_args()
def iou(a,b):
 x=max(a[0],b[0]);y=max(a[1],b[1]);xx=min(a[0]+a[2],b[0]+b[2]);yy=min(a[1]+a[3],b[1]+b[3])
 v=max(0,xx-x)*max(0,yy-y)
 return v/(a[2]*a[3]+b[2]*b[3]-v)
def match(gt,obs):
 pairs=sorted(((iou(b,[q[z] for z in ('x','y','w','h')]),k,j) for k,b in gt.items() for j,q in enumerate(obs)),reverse=True)
 used=set();out={}
 for score,k,j in pairs:
  if k not in out and j not in used:
   out[k]=(score,obs[j]['id']);used.add(j)
 return {k:out.get(k,(0,None)) for k in gt}
reports=[]
for p in sorted((H/'results').glob('*_fps*_stable.json')):
 if not args.historical and not p.name.startswith(('baseline_', 'candidate_')):continue
 fps=int(p.stem.split('_fps')[1].split('_')[0]);rows=json.loads(p.read_text());total=0;counts={.3:0,.5:0};details=[];last={};switches=0;average=0
 for t,gt in LABELS.items():
  ix=round((float(t)-5.5)*fps)
  if ix>=len(rows):continue
  row=rows[ix];res=match(gt,row['objects'])
  for k,(score,oid) in res.items():
   total+=1;average+=score
   for threshold in counts:counts[threshold]+=score>=threshold
   if score>=.3:
    switches+=k in last and last[k]!=oid;last[k]=oid
  details.append({'source_s':float(t),'matches':{k:{'iou':round(v[0],4),'id':v[1]} for k,v in res.items()}})
 report={'variant':p.stem,'replay_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'scoped_boxes':total,'matched_iou30':counts[.3],'matched_iou50':counts[.5],'mean_best_iou':round(average/total,4),'sparse_id_changes_iou30':switches,'details':details}
 reports.append(report);print({k:v for k,v in report.items() if k!='details'})
filename='sparse_scores_historical.json' if args.historical else 'sparse_scores.json'
(H/'results'/filename).write_text(json.dumps({'caveat':'Sparse 7-frame scoped labels, not exhaustive FP/recall/MOT metrics; 5fps labels nearest frame, offset at most 100ms. Partial vehicles retained. Mean IoU uses greedy one-to-one assignment.','labels_sha256':hashlib.sha256((H/'sparse_labels.json').read_bytes()).hexdigest(),'results':reports},indent=2)+'\n')
