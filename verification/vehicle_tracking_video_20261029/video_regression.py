"""Offline real-video A/B replay; no board access, training, or accuracy claims."""
from pathlib import Path
from collections import Counter
import subprocess, json, hashlib, time
import numpy as np
from PIL import Image, ImageDraw
H=Path(__file__).resolve().parent
P=H.parents[1]
F=Path(r'D:\Software for daily live\EVCapture\ffmpeg.exe')
G=Path(r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe')
V=Path(r'C:\Users\twofl\xwechat_files\wxid_e0zynr4vepxa22_8545\msg\video\2026-10\0d325868f3e3e5c37d0c592783d2852c.mp4')
D=P/'verification/motion_precision_20261029/tests/replay_capture.c'
O=H/'results';B=H/'build';A=H/'data'
for p in (O,B,A):p.mkdir(exist_ok=True)
def call(a):
 p=subprocess.run(list(map(str,a)),capture_output=True,text=True,encoding='utf-8',errors='replace')
 if p.returncode:raise RuntimeError(str(a)+'\n'+p.stdout+p.stderr)
 return p.stdout+p.stderr
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
def rgb(p):
 r=(p>>11)&31;g=(p>>5)&63;b=p&31
 return Image.fromarray(np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),2).astype('uint8'))
variants=('baseline','candidate');exes={}
manifest={'source':str(V),'source_sha256':sha(V),'ground_truth':False,'builds':{},'runs':[]}
for v in variants:
 s=H/'baseline' if v=='baseline' else P/'firmware/src'
 hashes={str(p):sha(p) for p in (s/'obstacle_motion_core.c',s/'obstacle_motion_core.h',D)}
 exe=B/(v+'.exe');args=[G,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',s,s/'obstacle_motion_core.c',D,'-o',exe]
 log=call(args);assert all(sha(p)==h for p,h in hashes.items()),'Source changed while compiling'
 (B/(v+'.log')).write_text(log)
 manifest['builds'][v]={'sources':hashes,'exe_sha256':sha(exe),'command':list(map(str,args))};exes[v]=exe
for fps in (5,10):
 for clip,start,end in (('full',0.0,None),('stable',5.5,15.0),('late_ui',16.0,None)):
  tag=f'fps{fps}_{clip}';folder=A/tag;folder.mkdir(exist_ok=True)
  raw=folder/'frames.rgb565';meta=folder/'metadata.bin'
  trim=f'trim=start={start}'+(f':end={end}' if end else '')
  vf=trim+f',setpts=PTS-STARTPTS,fps=fps={fps}:start_time=0:round=near,scale=320:146:flags=area,pad=320:240:0:47:black'
  call([F,'-v','error','-y','-i',V,'-an','-vf',vf,'-pix_fmt','rgb565le','-sws_dither','none','-f','rawvideo',raw])
  assert raw.stat().st_size%153600==0
  n=raw.stat().st_size//153600;assert n>0
  times=start+np.arange(n)/fps;stamps=np.rint(1000+times*1000).astype('<u4')
  m=np.zeros((n,16),dtype='<u4');m[:,0]=np.arange(1,n+1);m[:,2]=stamps;m[:,3]=stamps;m.tofile(meta)
  save(folder/'manifest.json',{'fps':fps,'start_s':start,'end_s':end,'frames':n,'filter':vf,'source_sha256':manifest['source_sha256'],'raw_sha256':sha(raw),'meta_sha256':sha(meta),'time_contract':'Resampled output PTS on source clock. Near rounding may select neighbouring original 22fps frame.'})
  frames=np.memmap(raw,dtype='<u2',mode='r',shape=(n,240,320));outputs={}
  for v in variants:
   out=O/f'{v}_{tag}.json';t=time.perf_counter();log=call([exes[v],raw,meta,out,'-']);elapsed=time.perf_counter()-t
   rows=json.loads(out.read_text());assert len(rows)==n
   for i,r in enumerate(rows):
    r['source_time_s']=round(float(times[i]),6);r['capture_ms']=int(stamps[i]);r['stable_interval']=bool(5.5<=times[i]<15)
    assert len({q['id'] for q in r['objects']})==r['count']
    for q in r['objects']:assert q['w']>0 and q['h']>0 and q['x']+q['w']<=320 and q['y']+q['h']<=240
   save(out,rows);outputs[v]=rows
   summary={'variant':v,'fps':fps,'clip':clip,'frames':n,'status_histogram':dict(Counter(r['status'] for r in rows)),'frames_with_boxes':sum(r['count']>0 for r in rows),'total_boxes':sum(r['count'] for r in rows),'max_boxes':max(r['count'] for r in rows),'unique_output_ids':len({q['id'] for r in rows for q in r['objects']}),'host_wall_seconds':elapsed,'host_wall_ms_per_frame':elapsed*1000/n,'caveat':'No GT: counts are not accuracy or ID-switch measurements. Host wall includes IO/JSON, not ARM or end-to-end latency. Only stable clip is used for primary comparison; full/late UI contain controls, pause and scaling changes.'}
   save(O/f'{v}_{tag}_summary.json',summary);manifest['runs'].append(summary);(O/f'{v}_{tag}.log').write_text(log)
  marks=(6,8,10,12,14) if clip=='stable' else ((16.5,18,20,22,24) if clip=='late_ui' else (6,10,14,18,22))
  sheet=Image.new('RGB',(1280,2500),(18,22,28))
  for j,t in enumerate(marks):
   i=int(np.argmin(np.abs(times-t)))
   for k,v in enumerate(variants):
    im=rgb(frames[i]).resize((640,480),Image.Resampling.NEAREST);d=ImageDraw.Draw(im)
    for q in outputs[v][i]['objects']:
     x,y,w,h=(q[z]*2 for z in ('x','y','w','h'));d.rectangle((x,y,x+w-1,y+h-1),outline=(0,255,100),width=2);d.text((x+2,y+2),str(q['id']),fill='yellow',stroke_width=1,stroke_fill='black')
    d.text((8,8),f'{v} {fps}fps source={times[i]:.2f}s count={outputs[v][i]["count"]}',fill='white');sheet.paste(im,(k*640,j*500))
  sheet.save(O/f'{tag}_contact.jpg',quality=92);del frames;save(O/'run_manifest.json',manifest)
  print(tag,'finished',flush=True)
print(O)
