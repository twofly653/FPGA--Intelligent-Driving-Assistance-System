import argparse
import json
from pathlib import Path
import cv2
import numpy as np

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('run',type=Path)
    parser.add_argument('--times',type=float,nargs='+',required=True)
    args=parser.parse_args()
    summary=json.loads((args.run/'summary.json').read_text(encoding='utf-8'))
    cap=cv2.VideoCapture(str(args.run/'tracked.mp4'))
    panels=[]
    for seconds in args.times:
        local=seconds-summary['start_frame']/summary['source_fps']
        cap.set(cv2.CAP_PROP_POS_MSEC,local*1000)
        ok,frame=cap.read()
        if not ok:raise RuntimeError(f'Cannot extract time {seconds}')
        height=round(frame.shape[0]*768/frame.shape[1])
        frame=cv2.resize(frame,(768,height),interpolation=cv2.INTER_AREA)
        panel_height=max(470,height+38)
        frame=cv2.copyMakeBorder(frame,0,panel_height-height,0,0,cv2.BORDER_CONSTANT,value=(20,20,20))
        cv2.putText(frame,f'{seconds:.1f}s - actual model / tracker output',(15,panel_height-15),
                    cv2.FONT_HERSHEY_SIMPLEX,.55,(255,255,255),1,cv2.LINE_AA)
        panels.append(frame)
    cap.release()
    while len(panels)%2:panels.append(np.zeros_like(panels[0]))
    contact=np.concatenate([np.concatenate(panels[i:i+2],axis=1) for i in range(0,len(panels),2)],axis=0)
    destination=args.run/'contact_sheet.jpg'
    assert cv2.imwrite(str(destination),contact,[cv2.IMWRITE_JPEG_QUALITY,94])
    print(destination)

if __name__=='__main__':main()
