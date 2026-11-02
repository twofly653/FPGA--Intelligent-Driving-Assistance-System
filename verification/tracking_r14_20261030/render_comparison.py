from pathlib import Path
import json,subprocess,hashlib
import numpy as np
from PIL import Image,ImageDraw
H=Path(__file__).parent;P=H.parents[1];F=Path(r'D:\Software for daily live\EVCapture\ffmpeg.exe')
def rgb(f):
 r=(f>>11)&31;g=(f>>5)&63;b=f&31;return Image.fromarray(np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),2).astype('uint8'))
def draw(f,row,label):
 im=rgb(f).resize((640,480),Image.Resampling.NEAREST);d=ImageDraw.Draw(im)
 for q in row['objects']:
  x,y,w,h=[q[z]*2 for z in ('x','y','w','h')];d.rectangle((x,y,x+w-1,y+h-1),outline=(0,255,80),width=2);d.text((x+2,y+2),str(q['id']),fill='yellow',stroke_width=1,stroke_fill='black')
 d.text((6,8),f'{label} {row["source_time_s"]:.1f}s',fill='white',stroke_width=1,stroke_fill='black');return im
report=[]
for video in ('bilibili','supplied'):
 raw=P/('verification/bilibili_tracking_20261030/data/fps10.rgb565' if video=='bilibili' else 'verification/vehicle_tracking_video_20261029/data/fps10_stable/frames.rgb565');frames=np.memmap(raw,dtype='<u2',shape=(raw.stat().st_size//153600,240,320));rows={v:json.loads((H/f'results/{video}_{v}_fps10.json').read_text()) for v in ('r13','r14')}
 out=H/f'{video}_R13_R14_comparison.mp4';cmd=[F,'-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1280x480','-r','10','-i','-','-an','-c:v','libx264','-crf','19','-pix_fmt','yuv420p',out];proc=subprocess.Popen(list(map(str,cmd)),stdin=subprocess.PIPE,stderr=subprocess.PIPE)
 for i,f in enumerate(frames):
  im=Image.new('RGB',(1280,480));im.paste(draw(f,rows['r13'][i],'R13'),(0,0));im.paste(draw(f,rows['r14'][i],'R14 candidate'),(640,0));proc.stdin.write(im.tobytes())
 proc.stdin.close();err=proc.stderr.read();assert proc.wait()==0,err
 checked=subprocess.run([F,'-v','error','-i',out,'-f','null','-'],capture_output=True);assert checked.returncode==0,checked.stderr
 marks=(2,4.9,7,10,13,16,19) if video=='bilibili' else (8,9,10,11,12,13,14)
 sheet=Image.new('RGB',(1280,480*len(marks)))
 for j,t in enumerate(marks):
  i=min(range(len(frames)),key=lambda i:abs(rows['r13'][i]['source_time_s']-t));sheet.paste(draw(frames[i],rows['r13'][i],'R13'),(0,j*480));sheet.paste(draw(frames[i],rows['r14'][i],'R14 candidate'),(640,j*480))
 sheet.save(H/f'{video}_comparison.jpg',quality=94)
 report.append({'video':video,'output':str(out),'sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'full_decode_ok':True,'frames':len(frames),'interpolation':False})
(H/'render_manifest.json').write_text(json.dumps(report,indent=2))
