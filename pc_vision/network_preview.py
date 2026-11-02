"""Compact presentation at 640x480; never changes detector coordinates."""
from track_video import cv2

def preview(frame,objects,stamp):
    height,width=frame.shape[:2];image=cv2.resize(frame,(640,480))
    used=[]
    for obj in sorted(objects,key=lambda o:o['bbox_xyxy'][3],reverse=True):
        x1,y1,x2,y2=obj['bbox_xyxy']
        x1,x2=round(x1*640/width),round(x2*640/width)
        y1,y2=round(y1*480/height),round(y2*480/height)
        cv2.rectangle(image,(x1,y1),(x2,y2),(0,255,70),2)
        text=f"#{obj['id']}"+(f" {obj['class_name']}" if x2-x1>=80 else '')
        (tw,th),baseline=cv2.getTextSize(text,cv2.FONT_HERSHEY_SIMPLEX,.36,1)
        left=min(max(0,x1),639-tw-5);top=max(23,y1-th-6)
        for dy in (0,-13,13,26,-26):
            t=min(479-th-baseline-4,max(23,top+dy))
            box=(left,t,left+tw+5,t+th+baseline+3)
            if not any(box[0]<b[2] and box[2]>b[0] and box[1]<b[3] and box[3]>b[1] for b in used):
                top=t;break
        used.append((left,top,left+tw+5,top+th+baseline+3))
        cv2.rectangle(image,(left,top),(left+tw+5,top+th+baseline+3),(0,85,20),-1)
        cv2.putText(image,text,(left+2,top+th+1),cv2.FONT_HERSHEY_SIMPLEX,.36,(255,255,255),1,cv2.LINE_AA)
    cv2.rectangle(image,(0,0),(639,21),(18,18,18),-1)
    cv2.putText(image,f'ETH GPU | {stamp:.2f}s | {len(objects)} tracks | YOLOv5su + ByteTrack',
                (8,15),cv2.FONT_HERSHEY_SIMPLEX,.39,(255,255,255),1,cv2.LINE_AA)
    return image
