from pathlib import Path
import json,hashlib,subprocess,time
import numpy as np
from PIL import Image,ImageDraw
P=Path(__file__).resolve().parents[2];H=Path(__file__).parent;O=H/'results';O.mkdir(exist_ok=True)
G=Path(r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe')
S=P/'firmware/src';T=P/'verification/motion_precision_20261029/tests/replay_capture.c'
BASE=P/'verification/bilibili_tracking_20261030/r13_baseline.exe'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(BASE)=='8d4d76c7a7ed1b762e9ca2fcad688feaff12596b0145cf5a42e557409211ba73'
def run(c):
 p=subprocess.run(list(map(str,c)),capture_output=True,text=True);assert p.returncode==0,p.stdout+p.stderr
 return p.stdout
run([G,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',S,S/'obstacle_motion_core.c',T,'-o',H/'r14.exe'])
manifest={'hardware_access':False,'r13_exe_sha':sha(BASE),'candidate_c_sha':sha(S/'obstacle_motion_core.c'),'candidate_h_sha':sha(S/'obstacle_motion_core.h'),'runs':[]}
for video in ('bilibili','supplied'):
 for fps in (5,10):
  folder=P/('verification/bilibili_tracking_20261030/data' if video=='bilibili' else f'verification/vehicle_tracking_video_20261029/data/fps{fps}_stable')
  raw=folder/(f'fps{fps}.rgb565' if video=='bilibili' else 'frames.rgb565');meta=folder/(f'fps{fps}.bin' if video=='bilibili' else 'metadata.bin')
  assert raw.exists() and meta.exists()
  for name,exe in [('r13',BASE),('r14',H/'r14.exe')]:
   out=O/f'{video}_{name}_fps{fps}.json';started=time.perf_counter();run([exe,raw,meta,out,'-']);elapsed=time.perf_counter()-started
   rows=json.loads(out.read_text());n=len(rows);start=0 if video=='bilibili' else 5.5
   prev={};shrinks=[]
   for i,r in enumerate(rows):
    r['source_time_s']=start+i/fps
    assert r['count']==len(r['objects'])<=8
    assert len(set(o['id'] for o in r['objects']))==r['count']
    for q in r['objects']:
     assert q['w']>0 and q['h']>0 and q['x']+q['w']<=320 and q['y']+q['h']<=240
     area=q['w']*q['h'];old=prev.get(q['id'])
     if old and 0<r['source_time_s']-old[0]<=0.5 and area<old[1]*0.5:shrinks.append({'time':r['source_time_s'],'id':q['id'],'old_area':old[1],'new_area':area})
     prev[q['id']]=(r['source_time_s'],area)
   out.write_text(json.dumps(rows))
   summary={'video':video,'variant':name,'fps':fps,'frames':n,'area_halving_events':shrinks,'frames_with_boxes':sum(r['count']>0 for r in rows),'unique_ids':len(set(o['id'] for r in rows for o in r['objects'])),'host_ms_per_frame':elapsed*1000/n,'raw_sha':sha(raw),'meta_sha':sha(meta)}
   manifest['runs'].append(summary);print(video,name,fps,'shrink_events',len(shrinks),'frames',n,flush=True)
(H/'manifest.json').write_text(json.dumps(manifest,indent=2))
# Use frozen sparse labels on the original supplied video, unchanged from R13.
labels=json.loads((P/'verification/vehicle_tracking_video_20261029/sparse_labels.json').read_text())['frames']
def iou(a,b):
 x=max(a[0],b[0]);y=max(a[1],b[1]);xx=min(a[0]+a[2],b[0]+b[2]);yy=min(a[1]+a[3],b[1]+b[3]);v=max(0,xx-x)*max(0,yy-y);return v/(a[2]*a[3]+b[2]*b[3]-v)
scores=[]
for fps in (5,10):
 for v in ('r13','r14'):
  rows=json.loads((O/f'supplied_{v}_fps{fps}.json').read_text());results=[];last={};idsw=0
  for t,gt in labels.items():
   row=rows[round((float(t)-5.5)*fps)];pairs=sorted([(iou(b,[o[z] for z in ('x','y','w','h')]),k,j) for k,b in gt.items() for j,o in enumerate(row['objects'])],reverse=True);assigned={};used=set()
   for score,k,j in pairs:
    if k not in assigned and j not in used: assigned[k]=(score,row['objects'][j]['id']);used.add(j)
   for k in gt:
    score,oid=assigned.get(k,(0,None));results.append(score)
    if score>=.3:idsw+=k in last and last[k]!=oid;last[k]=oid
  z={'variant':v,'fps':fps,'boxes':len(results),'iou30':sum(x>=.3 for x in results),'iou50':sum(x>=.5 for x in results),'mean_iou':sum(results)/len(results),'sparse_id_changes':idsw};scores.append(z);print(z)
(H/'sparse_scores.json').write_text(json.dumps(scores,indent=2))
