from pathlib import Path
import json,subprocess,numpy as np
from PIL import Image,ImageDraw
b=Path(r'D:\project\FPGA_PYNQ_Integrated\lcd_english_20261028\verification\motion_precision_20261029'); p=b/'build/r12_vehicle';s=b.parents[1]/'firmware/src'
gcc=r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe'
exe=p/'replay.exe'
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(s),str(s/'obstacle_motion_core.c'),str(b/'tests/replay_capture.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe),str(p/'obstacle_capture_frames.bin'),str(p/'obstacle_capture_meta.bin'),str(p/'replay.json'),str(p/'obstacle_capture_initial_state.bin')],check=True)
r=json.loads((p/'replay.json').read_text());meta=np.fromfile(p/'obstacle_capture_meta.bin',dtype='<u4').reshape(20,16);boxes=np.fromfile(p/'obstacle_capture_boxes.bin',dtype='<u4').reshape(20,8,8)
a=np.fromfile(p/'obstacle_capture_frames.bin',dtype='<u2').reshape(20,240,320);canvas=Image.new('RGB',(1280,1350));mismatch=[]
for i,pixels in enumerate(a):
 rr=(pixels>>11)&31;gg=(pixels>>5)&63;bb=pixels&31
 im=Image.fromarray(np.stack(((rr<<3)|(rr>>2),(gg<<2)|(gg>>4),(bb<<3)|(bb>>2)),axis=2).astype('uint8'));d=ImageDraw.Draw(im)
 n=int(meta[i,5])
 for q in boxes[i,:n]:
  x,y,w,h,identity=[int(v) for v in q[:5]];d.rectangle((x,y,x+w-1,y+h-1),outline=(0,255,0),width=1);d.text((x,y),str(identity),fill=(255,255,0))
 expected=[[int(v) for v in q[:5]] for q in boxes[i,:n]]
 actual=[[q[k] for k in ('x','y','w','h','id')] for q in r[i]['objects']]
 if expected!=actual or int(meta[i,4])!=r[i]['status']:mismatch.append(i)
 x0=i%4*320;y0=i//4*270;canvas.paste(im,(x0,y0));ImageDraw.Draw(canvas).text((x0,y0+242),f'{i}: count={n}, seq={meta[i,0]}',fill='white')
canvas.save(p/'contact.png')
summary={'frames':20,'count_per_frame':[int(m[5]) for m in meta],'span_ms':int(meta[-1,2]-meta[0,2]),'capture_intervals_ms':[int(x) for x in np.diff(meta[:,2])],'host_replay_mismatch_frames':mismatch,'capture_note':'On-board immutable analysis inputs and corresponding output; no user ground-truth labels.'}
(p/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
