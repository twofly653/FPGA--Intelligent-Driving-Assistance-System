"""In-memory wire-format + GPU replay. NO sockets, board, UART or flashing."""
from pathlib import Path
import argparse,hashlib,json,random,subprocess,time
from track_video import ROOT,YOLO,torch,cv2,np,annotate
from live_tracking import LiveTracker,HardwareSelection
from ethernet_tracking import decode_rgb565
from net_protocol import frame_packets,Reassembler,result_packet,unpack
from network_preview import preview

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    assert torch.cuda.is_available();model=YOLO(str(ROOT/'models/yolov5su.pt'))
    options=dict(device='0',half=True,imgsz=640,conf=.1,iou=.6,classes=[2,3,5,7],verbose=False)
    for _ in range(3):model.predict(np.zeros((240,320,3),np.uint8),**options)
    tracker=LiveTracker();selection=HardwareSelection();assembly=Reassembler(123);rng=random.Random(20261030)
    cap=cv2.VideoCapture(str(args.source));fps=cap.get(cv2.CAP_PROP_FPS);length=cap.get(cv2.CAP_PROP_FRAME_COUNT)/fps
    ffmpeg=Path(r'D:\Software for daily live\EVCapture\ffmpeg.exe');output=args.output/'tracked.mp4'
    encoder=subprocess.Popen([str(ffmpeg),'-v','error','-n','-f','rawvideo','-pix_fmt','bgr24','-s','640x480','-r','10','-i','-',
        '-an','-c:v','libx264','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(output)],stdin=subprocess.PIPE,stderr=subprocess.PIPE)
    rows=[];times=[];lost=0;class_before=class_after=0;previous={}
    try:
        for i in range(int(length*10)):
            cap.set(cv2.CAP_PROP_POS_FRAMES,round(i*fps/10));ok,original=cap.read()
            if not ok:break
            height=round(original.shape[0]*320/original.shape[1]);image=np.zeros((240,320,3),np.uint8)
            image[(240-height)//2:(240-height)//2+height]=cv2.resize(original,(320,height),interpolation=cv2.INTER_AREA)
            b,g,r=(image[:,:,k].astype(np.uint16) for k in range(3));raw=(((r>>3)<<11)|((g>>2)<<5)|(b>>3)).astype('<u2').tobytes()
            packets=list(frame_packets(raw,123,1,i+1,1000000+i*100000,320,240))
            if i%23==7:packets=packets[1:]
            if i%29==11:
                damaged=bytearray(packets[len(packets)//2]);damaged[-1]^=1;packets[len(packets)//2]=bytes(damaged)
            packets+=packets[:2];rng.shuffle(packets);complete=None
            for packet in packets:complete=assembly.accept(packet,i/10) or complete
            row={'source':'IN_MEMORY_UDP_REPLAY_NOT_HARDWARE','frame_index':i,'time_s':i/10,'width':320,'height':240,'hardware_synchronized':False,'objects':[]}
            if complete:
                header,pixels=complete;image=decode_rgb565(pixels,320,240);started=time.perf_counter()
                predictions=model.predict(image,**options)[0]
                all_objects=tracker.update(predictions.boxes,image,model.names,header.capture_us,header.epoch)
                elapsed=(time.perf_counter()-started)*1000;times.append(elapsed)
                objects=selection.choose(all_objects,i/10)
                result=result_packet(header,objects,elapsed);check,_=unpack(result);assert check.frame==i+1
                row['objects']=selection.visible_objects;row['hardware_objects']=objects;row['processing_ms']=elapsed;row['all_visible_count']=len(all_objects)
                for obj in all_objects:
                    old=previous.get(obj['id'])
                    if old and i/10-old[0]<=.4:
                        class_before+=obj['raw_class_id']!=old[1];class_after+=obj['class_id']!=old[2]
                    previous[obj['id']]=(i/10,obj['raw_class_id'],obj['class_id'])
                image=preview(image,selection.visible_objects,i/10)
            else:
                lost+=1;image=cv2.resize(image,(640,480));image[:]=20
                cv2.putText(image,'SIMULATED PACKET LOSS',(45,235),cv2.FONT_HERSHEY_SIMPLEX,.65,(0,100,255),1,cv2.LINE_AA)
            rows.append(row);encoder.stdin.write(cv2.resize(image,(640,480)).tobytes())
            if i%50==0:print(json.dumps({'sample':i,'objects':len(row['objects']),'dropped_frames':lost}),flush=True)
    finally:cap.release();encoder.stdin.close()
    error=encoder.stderr.read();assert encoder.wait(timeout=60)==0,error
    decode=subprocess.run([str(ffmpeg),'-v','error','-i',str(output),'-f','null','-'],capture_output=True);assert decode.returncode==0,decode.stderr
    (args.output/'tracks.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows),encoding='utf-8')
    meta={'replay_only':True,'sockets_used':False,'board_access':False,'source':str(args.source),'processed_frames':len(rows),
          'source_width':320,'source_height':240,'source_fps':10,'start_frame':0,'rgb565_wire':True,'capture_interval_ms':100,
          'packet_loss_injected':True,'frames_without_complete_payload':lost,'reassembly':assembly.stats,
          'compute_ms_median':float(np.median(times)),'compute_ms_p95':float(np.percentile(times,95)),
          'raw_label_changes':class_before,'smoothed_label_changes':class_after,'full_output_decode_passed':True,
          'weights':'YOLOv5su COCO, not TomMao PSP','source_sha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),
          'output_sha256':hashlib.sha256(output.read_bytes()).hexdigest()}
    (args.output/'summary.json').write_text(json.dumps(meta,indent=2));print(json.dumps(meta),flush=True)

if __name__=='__main__':main()
