"""Pure offline RGB565 regression, no board/JTAG/serial I/O.
Synthetic stimuli are labelled separately from the genuine static capture.
"""
from pathlib import Path
import argparse, subprocess, json, time, hashlib
import numpy as np
B=Path(__file__).resolve().parent.parent
SRC=B.parents[1]/'firmware'/'src'
GCC=Path(r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe')
ARM=Path(r'D:\FPGA\2026.1\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe')
REAL=Path(r'D:\project\FPGA_PYNQ_Integrated\camera_optimized_20261025\board_validation\r6_20261026\static_rgb565.bin')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=B/'build',help='Evidence output directory; use a new directory for a new revision.')
OUT=parser.parse_args().output.resolve();OUT.mkdir(parents=True,exist_ok=True)
def command(args):
 p=subprocess.run(list(map(str,args)),capture_output=True,text=True)
 if p.returncode: raise RuntimeError(p.stdout+p.stderr)
 return p.stdout
for name,src in [('before',B/'before'),('after',SRC)]:
 print(command([GCC,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',src,src/'obstacle_motion_core.c',B/'tests'/'obstacle_replay.c','-o',OUT/(name+'.exe')]))
command([ARM,'-std=c11','-O2','-Wall','-Wextra','-Werror','-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=hard','-c',SRC/'obstacle_motion_core.c','-o',OUT/'motion_arm.o'])
y,x=np.indices((240,320));rng=np.random.default_rng(20261029)
def pack(a):
 a=np.clip(a,0,255).astype(np.uint16)
 return ((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)
def unpack(p):
 p=p.astype(np.uint16);r=(p>>11)&31;g=(p>>5)&63;bl=p&31
 return np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(bl<<3)|(bl>>2)),axis=2).astype(np.int16)
flat=np.full((240,320,3),64,dtype=np.int16)
texture=np.stack([55+(x//12%2)*50+(y//16%2)*35]*3,axis=2).astype(np.int16)
real=np.fromfile(REAL,dtype='<u2').reshape(-1,240,320)
photo=unpack(real[0])
cases=[]
def add(name,frames,truth,kind='synthetic',skip=0):cases.append((name,frames,truth,kind,skip))
def moving(name,objects,background=flat,n=24,skip=3,stop=True):
 fs=[pack(background)]*5;gt=[[]]*5
 for t in range(n):
  a=background.copy();boxes=[]
  for f in objects:
   bx,by,bw,bh,color=f(t);xa=max(0,bx);ya=max(0,by);xb=min(320,bx+bw);yb=min(240,by+bh)
   a[ya:yb,xa:xb]=color;boxes.append([xa,ya,xb-xa,yb-ya])
  fs.append(pack(a));gt.append(boxes if t>=skip else None)
 if stop:
  fs.extend([fs[-1]]*20);gt.extend([gt[-1]]*20)
  fs.extend([pack(background)]*6);gt.extend([[]]*6)
 add(name,fs,gt,skip=skip)
moving('one', [lambda t:(12+t*6,60,32,44,[210]*3)])
moving('two', [lambda t:(20+t*4,35,34,42,[210]*3),lambda t:(252-t*3,142,30,36,[165]*3)])
moving('neighbours',[lambda t:(30+t*4,90,30,40,[210]*3),lambda t:(70+t*4,90,30,40,[185]*3)])
moving('eight',[lambda t,i=i:(10+i%4*72+t*2,18+i//4*110,20,28,[200]*3) for i in range(8)],n=12)
moving('edge',[lambda t:(-6+t*4,80,28,40,[210]*3)],n=20)
moving('slow',[lambda t:(30+t,70,36,44,[210]*3)],n=32,skip=8)
moving('fast',[lambda t:(10+t*20,70,32,44,[210]*3)],n=12)
neutral=np.full_like(flat,120)
moving('equal_luma_colours',[lambda t:(24+t*4,130,30,40,[210,82,48]),lambda t:(240-t*2,55,20,40,[42,130,210])],background=neutral,n=22)
moving('textured_background',[lambda t:(14+t*6,65,36,48,[20,20,20])],background=texture,n=24)
moving('camera_texture_background',[lambda t:(14+t*6,65,36,48,[220,50,30])],background=photo,n=24)
# Stationary glitches must not become objects.
fs=[pack(flat)]*5;gt=[[]]*5
for t in range(32):
 a=flat.copy()
 if t%4==0:a[65:110,35:75]=220
 fs.append(pack(a));gt.append([])
add('one_frame_flash',fs,gt)
fs=[pack(flat)]*5;gt=[[]]*5
for t in range(32):
 a=flat.copy();a[65:110,35:75]=220 if t%2 else 140
 fs.append(pack(a));gt.append([])
add('stationary_flicker',fs,gt)
# Global gain on structured background: additive compensation alone is insufficient.
for bg,label in [(texture,'pattern'),(photo,'camera_texture')]:
 fs=[pack(bg)]*5;gt=[[]]*5
 for t in range(32):
  gain=[1.0,1.18,0.88,1.12,0.96][t%5];a=np.clip(bg*gain+3,0,255)
  fs.append(pack(a));gt.append([])
 add('exposure_gain_'+label,fs,gt)
# Textured moving shadow: actual texture remains under multiplicative darkening.
fs=[pack(texture)]*5;gt=[[]]*5
for t in range(26):
 a=texture.copy();xx=12+t*7;a[50:140,xx:xx+60]=(a[50:140,xx:xx+60]*0.6).astype(np.int16)
 fs.append(pack(a));gt.append([])
add('moving_shadow',fs,gt)
for label,sigma in [('low',5),('high',20)]:
 fs=[];gt=[]
 for t in range(36):
  a=flat+rng.normal(0,sigma,flat.shape);a=np.clip(a,0,255)
  for _ in range(40):a[rng.integers(0,240),rng.integers(0,320)]=255
  fs.append(pack(a));gt.append([])
 add('sensor_noise_'+label,fs,gt)
fs=[pack(texture)]*5;gt=[[]]*5
for t in range(20):
 fs.append(pack(np.roll(texture,2*(t%3),axis=1)));gt.append([])
add('camera_shake',fs,gt)
add('real_static_capture',[f for f in real],[[]]*len(real),kind='genuine_camera_sequence')
def iou(a,b):
 xa=max(a[0],b[0]);ya=max(a[1],b[1]);xb=min(a[0]+a[2],b[0]+b[2]);yb=min(a[1]+a[3],b[1]+b[3]);inter=max(0,xb-xa)*max(0,yb-ya)
 return inter/(a[2]*a[3]+b[2]*b[3]-inter)
report={'description':'Offline synthetic behavior regression plus genuine 20-frame static regression. Not real moving-object accuracy.','source_sha256':hashlib.sha256((SRC/'obstacle_motion_core.c').read_bytes()).hexdigest(),'cases':[]}
for name,fs,gt,kind,skip in cases:
 inp=OUT/(name+'.rgb565');np.stack(fs).astype('<u2').tofile(inp)
 row={'name':name,'kind':kind,'frames':len(fs),'acquisition_frames_excluded':skip}
 for version in ['before','after']:
  op=OUT/(name+'_'+version+'.json');begin=time.perf_counter();command([OUT/(version+'.exe'),inp,op]);elapsed=time.perf_counter()-begin
  rs=json.loads(op.read_text());fail=[];false=miss=good=0;first=None;ids={};switches=0
  for t,(r,truth) in enumerate(zip(rs,gt)):
   obs=r['objects']
   if obs and first is None:first=t
   if len(obs)>8 or any(o['x']+o['w']>320 or o['y']+o['h']>240 or not o['w'] or not o['h'] for o in obs):raise AssertionError('bounds')
   if truth is None:continue
   if not truth:
    false+=len(obs)
    if obs:fail.append(t)
    continue
   used=set()
   for j,box in enumerate(truth):
    scores=[iou([o['x'],o['y'],o['w'],o['h']],box) if k not in used else 0 for k,o in enumerate(obs)]
    k=max(range(len(scores)),key=lambda k:scores[k]) if scores else -1
    if k<0 or scores[k]<0.55:miss+=1;fail.append(t);continue
    good+=1;used.add(k)
    if j in ids and ids[j]!=obs[k]['id']:switches+=1
    ids[j]=obs[k]['id']
   false+=len(obs)-len(used)
   if len(obs)>len(truth):fail.append(t)
  row[version]={'false_boxes':false,'missed_targets':miss,'matched_targets':good,'id_switches':switches,'failing_frames':sorted(set(fail)),'first_box_frame':first,'elapsed_ms_including_process_start':round(elapsed*1000,2)}
 report['cases'].append(row)
 print(name, 'before=',row['before']['false_boxes'],row['before']['missed_targets'],'after=',row['after']['false_boxes'],row['after']['missed_targets'],flush=True)
report['after_failures']=[r['name'] for r in report['cases'] if r['after']['false_boxes'] or r['after']['missed_targets'] or r['after']['id_switches']]
(OUT/'regression.json').write_text(json.dumps(report,indent=2))
print('CANDIDATE_FAILURES',report['after_failures'])

if report["after_failures"]: raise SystemExit(1)
