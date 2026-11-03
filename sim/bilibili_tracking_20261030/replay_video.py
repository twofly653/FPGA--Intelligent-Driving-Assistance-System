"""Replay actual production tracker against the public source video; no board access."""
from pathlib import Path
from collections import Counter
import argparse, hashlib, json, subprocess, time
import numpy as np
from PIL import Image, ImageDraw
H=Path(__file__).resolve().parent
P=H.parents[1]
F=Path(r"D:\Software for daily live\EVCapture\ffmpeg.exe")
G=Path(r"D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe")
V=H/"BV11x411R7o6_p1_car.mp4"
D=P/"sim/motion_precision_20261029/tests/replay_capture.c"
O=H/"results"; A=H/"data"; B=H/"build"
for q in (O,A,B):q.mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+"\n",encoding="utf-8")
def call(a):
 r=subprocess.run(list(map(str,a)),capture_output=True,text=True,encoding="utf-8",errors="replace")
 if r.returncode:raise RuntimeError(str(a)+"\n"+r.stdout+r.stderr)
 return r.stdout+r.stderr
def rgb(p):
 r=(p>>11)&31;g=(p>>5)&63;b=p&31
 return Image.fromarray(np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),2).astype("uint8"))
def draw(frame,row,label):
 im=rgb(frame).resize((640,480),Image.Resampling.NEAREST);d=ImageDraw.Draw(im)
 for q in row["objects"]:
  x,y,w,h=(q[z]*2 for z in ("x","y","w","h"))
  d.rectangle((x,y,x+w-1,y+h-1),outline=(0,255,80),width=2)
  d.text((x+2,y+2),str(q["id"]),fill="yellow",stroke_width=1,stroke_fill="black")
 d.text((5,5),label+f" t={row['source_time_s']:.1f}s n={row['count']} state={row['status']}",fill="white",stroke_width=1,stroke_fill="black")
 return im
ap=argparse.ArgumentParser();ap.add_argument("--candidate",action="store_true");args=ap.parse_args()
variant="candidate" if args.candidate else "r13_baseline"
exe=B/"candidate.exe" if args.candidate else H/"r13_baseline.exe"
manifest={"source_url":"https://www.bilibili.com/video/BV11x411R7o6?t=4.9","page":1,"cid":99397548,"source_sha256":sha(V),"dense_ground_truth":False,"hardware_access":False,"variant":variant,"normalization":"320x180 area resampling, 30px vertical black padding, RGB565LE","runs":[]}
if args.candidate:
 s=P/"src/firmware/src";paths=(s/"obstacle_motion_core.c",s/"obstacle_motion_core.h",D)
 hashes={str(q):sha(q) for q in paths}
 cmd=[G,"-std=c11","-O2","-Wall","-Wextra","-Werror","-I",s,paths[0],D,"-o",exe]
 (B/"candidate_build.log").write_text(call(cmd))
 if any(sha(q)!=v for q,v in hashes.items()):raise RuntimeError("source changed during build")
 manifest["source_hashes"]=hashes;manifest["build_command"]=list(map(str,cmd))
else:
 manifest["source_hashes"]={"core_c":"22703779070ab92bafdec7871c20c735594fac95b7311dc44b1df0e718e784bd","core_h":"8f438d7ed8e8cd0fb3cc8d6dea12ef98c7d8a51db7cecb9a23ea389f4547d388"}
 if sha(exe)!="8d4d76c7a7ed1b762e9ca2fcad688feaff12596b0145cf5a42e557409211ba73":raise RuntimeError("baseline executable mismatch")
manifest["executable_sha256"]=sha(exe)
for fps in (5,10):
 raw=A/f"fps{fps}.rgb565";meta=A/f"fps{fps}.bin"
 if not raw.exists():
  call([F,"-v","error","-y","-i",V,"-an","-vf",f"fps={fps}:start_time=0:round=near,scale=320:180:flags=area,pad=320:240:0:30:black","-pix_fmt","rgb565le","-sws_dither","none","-f","rawvideo",raw])
 if raw.stat().st_size%153600:raise RuntimeError("incomplete frame")
 n=raw.stat().st_size//153600
 stamps=np.rint(1000+np.arange(n)*1000/fps).astype("<u4")
 m=np.zeros((n,16),dtype="<u4");m[:,0]=np.arange(1,n+1);m[:,2]=stamps;m[:,3]=stamps
 if not meta.exists():m.tofile(meta)
 out=O/f"{variant}_fps{fps}.json";t=time.perf_counter();log=call([exe,raw,meta,out,"-"]);elapsed=time.perf_counter()-t
 rows=json.loads(out.read_text())
 if len(rows)!=n:raise RuntimeError("replay frame count mismatch")
 for i,r in enumerate(rows):
  r["source_time_s"]=i/fps;r["capture_ms"]=int(stamps[i])
  assert len({q["id"] for q in r["objects"]})==r["count"]
  for q in r["objects"]:assert q["w"]>0 and q["h"]>0 and q["x"]+q["w"]<=320 and q["y"]+q["h"]<=240
 save(out,rows);(O/f"{variant}_fps{fps}.log").write_text(log)
 summary={"fps":fps,"frames":n,"status_histogram":dict(Counter(r["status"] for r in rows)),"frames_with_boxes":sum(r["count"]>0 for r in rows),"total_boxes":sum(r["count"] for r in rows),"unique_ids":len({q["id"] for r in rows for q in r["objects"]}),"host_wall_ms_per_frame":elapsed*1000/n,"raw_sha256":sha(raw),"metadata_sha256":sha(meta),"caveat":"Counts and IDs are descriptive, not accuracy. Host wall includes IO, not ARM latency."}
 manifest["runs"].append(summary)
 frames=np.memmap(raw,dtype="<u2",mode="r",shape=(n,240,320))
 base=json.loads((O/f"r13_baseline_fps{fps}.json").read_text()) if args.candidate else rows
 marks=(2.,4.9,7.,10.,13.,16.,19.)
 sheet=Image.new("RGB",(1280,480*len(marks)),(18,22,28))
 for j,t in enumerate(marks):
  i=min(round(t*fps),n-1)
  sheet.paste(draw(frames[i],base[i],"R13 baseline"),(0,j*480))
  sheet.paste(draw(frames[i],rows[i],variant),(640,j*480))
 sheet.save(O/f"{variant}_fps{fps}_contact.jpg",quality=94)
 if args.candidate and fps==10:
  cmd=[str(F),"-v","error","-y","-f","rawvideo","-pix_fmt","rgb24","-s","1280x480","-r",str(fps),"-i","-","-an","-c:v","libx264","-crf","19","-pix_fmt","yuv420p",str(O/"R13_R14_comparison.mp4")]
  process=subprocess.Popen(cmd,stdin=subprocess.PIPE,stderr=subprocess.PIPE)
  for i in range(n):
   im=Image.new("RGB",(1280,480));im.paste(draw(frames[i],base[i],"R13 baseline"),(0,0));im.paste(draw(frames[i],rows[i],variant),(640,0));process.stdin.write(im.tobytes())
  process.stdin.close();err=process.stderr.read();rc=process.wait()
  if rc:raise RuntimeError(err.decode(errors="replace"))
 del frames
 print(variant,summary,flush=True)
save(O/f"{variant}_manifest.json",manifest)
