from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
H=Path(__file__).parent;P=H.parents[1];raw=P/'sim/bilibili_tracking_20261030/data/fps10.rgb565';fs=np.memmap(raw,dtype='<u2',shape=(raw.stat().st_size//153600,240,320));sheet=Image.new('RGB',(1280,960))
for j,t in enumerate((2,7,13,19)):
 f=fs[t*10];r=(f>>11)&31;g=(f>>5)&63;b=f&31;im=Image.fromarray(np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),2).astype('uint8')).resize((640,480));d=ImageDraw.Draw(im)
 for x in range(0,321,20):d.line((x*2,60,x*2,420),fill=(55,85,120),width=1);d.text((x*2+1,45),str(x),fill='white')
 for y in range(40,220,20):d.line((0,y*2,640,y*2),fill=(55,85,120),width=1);d.text((0,y*2+1),str(y),fill='white')
 d.text((10,10),f'Source {t}s / input coordinates',fill='white');sheet.paste(im,((j%2)*640,(j//2)*480))
sheet.save(H/'bilibili_label_grid.png')
