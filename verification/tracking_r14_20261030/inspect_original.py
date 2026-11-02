from pathlib import Path
import json,numpy as np
from PIL import Image,ImageDraw
H=Path(__file__).parent;P=H.parents[1];raw=P/'verification/vehicle_tracking_video_20261029/data/fps10_stable/frames.rgb565';fs=np.memmap(raw,dtype='<u2',shape=(raw.stat().st_size//153600,240,320));out=Image.new('RGB',(1280,480*4));truth=json.loads((P/'verification/vehicle_tracking_video_20261029/sparse_labels.json').read_text())['frames']
for j,t in enumerate((8,10,12,14)):
 i=round((t-5.5)*10);f=fs[i];r=(f>>11)&31;g=(f>>5)&63;b=f&31;a=np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),2).astype('uint8')
 for k,v in enumerate(('r13','r14')):
  rows=json.loads((H/f'results/supplied_{v}_fps10.json').read_text());im=Image.fromarray(a).resize((640,480));d=ImageDraw.Draw(im)
  for q in rows[i]['objects']:
   x,y,w,h=[q[z]*2 for z in ('x','y','w','h')];d.rectangle((x,y,x+w,y+h),outline='lime',width=2);d.text((x,y),str(q['id']),fill='yellow')
  for n,q in truth[str(t)].items():
   x,y,w,h=[z*2 for z in q];d.rectangle((x,y,x+w,y+h),outline='red',width=1);d.text((x,y+h),n,fill='red')
  d.text((8,8),f'{v} {t}s',fill='white');out.paste(im,(k*640,j*480))
out.save(H/'supplied_debug.png')
