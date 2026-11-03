"""Render actual logged offline results, without rerunning or inventing tracks."""
import argparse,json,subprocess
from pathlib import Path
from track_video import cv2,np
from quality_input import camera_input
from network_preview import preview

FFMPEG=Path(r'D:\Software for daily live\EVCapture\ffmpeg.exe')

def make(run):
    meta=json.loads((run/'summary.json').read_text());rows=[json.loads(s) for s in (run/'tracks.jsonl').read_text().splitlines()]
    cap=cv2.VideoCapture(meta['source']);fps=cap.get(cv2.CAP_PROP_FPS)
    # Pipe raw frames into H.264 directly, no intermediate AVI and no interpolation.
    command=[str(FFMPEG),'-v','error','-y','-f','rawvideo','-pix_fmt','bgr24','-s','640x480',
             '-r',str(meta['source_fps']),'-i','-','-an','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(run/'tracked.mp4')]
    encode=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    contact=None
    for row in rows:
        cap.set(cv2.CAP_PROP_POS_FRAMES,round(row['time_s']*fps));ok,frame=cap.read();assert ok
        _,image=camera_input(frame,meta['profile']);view=preview(image,row['objects'],row['time_s'])
        cv2.rectangle(view,(0,0),(639,22),(18,18,18),-1)
        caption=f"OFFLINE | {meta['profile']} | {meta['source_fps']}Hz replay | t={row['time_s']:.2f}s"
        cv2.putText(view,caption,(8,15),cv2.FONT_HERSHEY_SIMPLEX,.39,(255,255,255),1,cv2.LINE_AA)
        cv2.rectangle(view,(0,453),(639,479),(18,18,18),-1)
        note='NO BOARD / NO ETHERNET TEST - simulation only'
        if not row['complete']:note='SIMULATED PACKET LOSS - no tracking result for this frame'
        cv2.putText(view,note,(8,470),cv2.FONT_HERSHEY_SIMPLEX,.38,(170,220,255),1,cv2.LINE_AA)
        encode.stdin.write(view.tobytes())
        if abs(row['time_s']-14.)<.025:contact=view.copy();cv2.imwrite(str(run/'frame14.png'),view)
    cap.release();encode.stdin.close();error=encode.stderr.read();code=encode.wait()
    if code:raise RuntimeError(error.decode(errors='replace'))
    return contact

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);a=ap.parse_args()
    paths=['legacy320_10hz_i640_baseline','antialias320_10hz_i640_precision','native640_10hz_i640_precision','native640_15hz_i640_precision']
    views=[make(a.root/p) for p in paths]
    cv2.imwrite(str(a.root/'comparison.png'),np.concatenate([np.concatenate(views[:2],axis=1),np.concatenate(views[2:],axis=1)],axis=0))
    print('OFFLINE_RENDER_COMPLETE')
