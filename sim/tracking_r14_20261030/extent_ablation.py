from pathlib import Path
import subprocess,json
H=Path(__file__).parent;P=H.parents[1];S=P/'src/firmware/src';G=Path(r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe');T=P/'sim/motion_precision_20261029/tests/replay_capture.c';base=(H/'r13_reconstructed.c').read_text();labels=json.loads((P/'sim/vehicle_tracking_video_20261029/sparse_labels.json').read_text())['frames'];A=H/'ablation';A.mkdir(exist_ok=True)
def iou(a,b):
 x=max(a[0],b[0]);y=max(a[1],b[1]);xx=min(a[0]+a[2],b[0]+b[2]);yy=min(a[1]+a[3],b[1]+b[3]);v=max(0,xx-x)*max(0,yy-y);return v/(a[2]*a[3]+b[2]*b[3]-v)
reports=[]
for low,step in [(70,15),(80,10),(90,5)]:
 for penalty in (64,256,512):
  s=base.replace('(70u+15u*scale)',f'({low}u+{step}u*scale)');s=s.replace('score=score*8u+(unsigned)(iabs(xx-cx)+iabs(yy-cy));',f'score=score*8u+(unsigned)(iabs(xx-cx)+iabs(yy-cy));\n            score+=(unsigned)(iabs((int)w-t->bbox.w)+iabs((int)h-t->bbox.h))*{penalty}u/umax(1u,t->bbox.w+t->bbox.h);')
  tag=f't{low}_p{penalty}';src=A/(tag+'.c');src.write_text(s);exe=A/(tag+'.exe');subprocess.run([G,'-std=c11','-O2','-I',S,src,T,'-o',exe],check=True,capture_output=True)
  scores=[]
  for video in ('supplied','bilibili'):
   for fps in (5,10):
    folder=P/('sim/bilibili_tracking_20261030/data' if video=='bilibili' else f'sim/vehicle_tracking_video_20261029/data/fps{fps}_stable');raw=folder/(f'fps{fps}.rgb565' if video=='bilibili' else 'frames.rgb565');meta=folder/(f'fps{fps}.bin' if video=='bilibili' else 'metadata.bin');out=A/f'{tag}_{video}{fps}.json';subprocess.run([exe,raw,meta,out,'-'],check=True,capture_output=True);rows=json.loads(out.read_text());prev={};shrinks=0
    for i,r in enumerate(rows):
     for q in r['objects']:
      area=q['w']*q['h'];old=prev.get(q['id']);shrinks+=bool(old and 0<i-old[0]<=fps*.5 and area<old[1]*.5);prev[q['id']]=(i,area)
    z={'video':video,'fps':fps,'shrinks':shrinks}
    if video=='supplied':
     vals=[]
     for t,gt in labels.items():
      r=rows[round((float(t)-5.5)*fps)];pairs=sorted([(iou(b,[q[k] for k in ('x','y','w','h')]),key,j) for key,b in gt.items() for j,q in enumerate(r['objects'])],reverse=True);taken=set();ids=set();res={}
      for score,key,j in pairs:
       if key not in ids and j not in taken:res[key]=score;taken.add(j);ids.add(key)
      vals.extend(res.get(k,0) for k in gt)
     z.update(iou50=sum(v>=.5 for v in vals),mean=sum(vals)/len(vals))
    scores.append(z)
  print(tag,scores,flush=True);reports.append({'tag':tag,'scores':scores})
(H/'ablation_summary.json').write_text(json.dumps(reports,indent=2))
