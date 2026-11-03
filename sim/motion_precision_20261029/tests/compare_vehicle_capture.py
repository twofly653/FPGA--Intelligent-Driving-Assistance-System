from pathlib import Path
from PIL import Image,ImageDraw
import numpy as np,json,subprocess
b=Path(__file__).resolve().parent.parent;out=b/'build';src=b.parents[1]/'src'/'firmware'/'src'
gcc=Path(r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe')
a=np.fromfile(out/'obstacle_capture_frames.bin',dtype='<u2').reshape(-1,240,320)
for name,source in [('r11',b/'before_tracking'),('tracking',src)]:
 exe=out/f'replay_capture_{name}.exe'
 subprocess.run(list(map(str,[gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',source,source/'obstacle_motion_core.c',b/'tests'/'replay_capture.c','-o',exe])),check=True)
 for mode,initial in [('imported',out/'obstacle_capture_initial_state.bin'),('reset','-')]:
  op=out/f'vehicle_{name}_{mode}.json'
  subprocess.run(list(map(str,[exe,out/'obstacle_capture_frames.bin',out/'obstacle_capture_meta.bin',op,initial])),check=True)
  rs=json.loads(op.read_text());canvas=Image.new('RGB',(1280,1350))
  for i,(p,row) in enumerate(zip(a,rs)):
   rr=(p>>11)&31;gg=(p>>5)&63;bb=p&31
   im=Image.fromarray(np.stack(((rr<<3)|(rr>>2),(gg<<2)|(gg>>4),(bb<<3)|(bb>>2)),axis=2).astype('uint8'));d=ImageDraw.Draw(im)
   for q in row['objects']:
    x,y,w,h=[q[k] for k in ('x','y','w','h')];d.rectangle((x,y,x+w-1,y+h-1),outline=(0,255,0),width=1);d.text((x,y),str(q['id']),fill=(255,255,0))
   x0=i%4*320;y0=i//4*270;canvas.paste(im,(x0,y0));ImageDraw.Draw(canvas).text((x0,y0+242),f"{i}: {row['count']} {name} {mode}",fill='white')
  canvas.save(out/f'vehicle_{name}_{mode}.png')
  print(name,mode,'counts',[r['count'] for r in rs])
