"""Local-file GPU + wire-format comparison. NO sockets or board access."""
from pathlib import Path
import argparse,json,time,random
from track_video import ROOT,YOLO,torch,cv2,np,sha256
from net_protocol import frame_packets,Reassembler,result_packet,unpack
from live_tracking import LiveTracker,HardwareSelection
from precision_tracking import PrecisionTracker
from quality_input import camera_input
from evaluate_sparse import evaluate

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--labels',choices=['supplied','bilibili'],default='supplied')
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    assert torch.cuda.is_available();model=YOLO(str(ROOT/'models/yolov5su.pt'))
    cap=cv2.VideoCapture(str(a.source));fps=cap.get(cv2.CAP_PROP_FPS);frames=[]
    while True:
        ok,frame=cap.read()
        if not ok:break
        frames.append(frame)
    cap.release();assert frames
    reports=[]
    for profile,hz,size in [('legacy320',10,640),('antialias320',10,640),('native640',10,640),('native640',10,960),('native640',15,640)]:
        runs={kind:a.output/f'{profile}_{hz}hz_i{size}_{kind}' for kind in ('baseline','precision')}
        for p in runs.values():p.mkdir()
        trackers={'baseline':LiveTracker(),'precision':PrecisionTracker()}
        selectors={k:HardwareSelection() for k in trackers};rows={k:[] for k in trackers};times={k:[] for k in trackers}
        options=dict(device='0',half=True,imgsz=size,conf=.1,iou=.6,classes=[2,3,5,7],verbose=False)
        shape=(480,640,3) if profile=='native640' else (240,320,3)
        for _ in range(3):model.predict(np.zeros(shape,np.uint8),**options)
        reassembly=Reassembler(123);rng=random.Random(20261030);lost=0;wire_bytes=0
        for i in range(int(len(frames)/fps*hz)):
            stamp=i/hz;raw,image=camera_input(frames[min(len(frames)-1,round(stamp*fps))],profile);h,w=image.shape[:2]
            packets=list(frame_packets(raw,123,1,i+1,1000000+round(stamp*1e6),w,h))
            wire_bytes+=sum(len(p)+28 for p in packets)
            if i%23==7:packets=packets[1:]
            if i%29==11:
                corrupt=bytearray(packets[len(packets)//2]);corrupt[-1]^=1;packets[len(packets)//2]=bytes(corrupt)
            packets+=packets[:2];rng.shuffle(packets);complete=None
            for packet in packets:complete=reassembly.accept(packet,stamp) or complete
            if complete:
                header,assembled=complete;assert assembled==raw
                started=time.perf_counter();det=model.predict(image,**options)[0];det.boxes.cpu();det_ms=(time.perf_counter()-started)*1000
            else:lost+=1
            for kind,tracker in trackers.items():
                row={'frame_index':i,'time_s':stamp,'width':w,'height':h,'objects':[],'board_access':False,'complete':bool(complete)}
                if complete:
                    started=time.perf_counter();objects=tracker.update(det.boxes,image,model.names,header.capture_us,header.epoch)
                    ms=det_ms+(time.perf_counter()-started)*1000;times[kind].append(ms)
                    chosen=selectors[kind].choose(objects,stamp)
                    response=result_packet(header,chosen,ms);assert unpack(response)[0].frame==i+1
                    row.update(objects=selectors[kind].visible_objects,hardware_objects=chosen,processing_ms=ms)
                rows[kind].append(row)
        for kind,path in runs.items():
            metadata={'source':str(a.source),'source_sha256':sha256(a.source),'profile':profile,'source_fps':hz,'source_width':w,'source_height':h,
                      'start_frame':0,'rgb565_wire':True,'replay_only':True,'board_access':False,'sockets_used':False,'tracker':kind,'imgsz':size,
                      'processed_frames':len(rows[kind]),'complete_frames':len(times[kind]),'frames_without_complete_payload':lost,
                      'compute_ms_median':float(np.median(times[kind])),'compute_ms_p95':float(np.percentile(times[kind],95)),
                      'nominal_udp_ip_mbps':wire_bytes*8/(len(rows[kind])/hz)/1e6,
                      'note':'Sampling rate is simulated. native640 transport producer is not implemented on the board.'}
            (path/'summary.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
            (path/'tracks.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows[kind]),encoding='utf-8')
            evaluate(path,a.labels)
            metrics=json.loads((path/'sparse_evaluation.json').read_text())
            record={**metadata,'run':str(path),'sparse_iou50':metrics['iou50_matches'],'sparse_mean_iou':metrics['mean_best_matched_iou'],'sparse_id_changes':metrics['sparse_id_changes']}
            reports.append(record);print(json.dumps(record),flush=True)
    (a.output/'comparison.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')

if __name__=='__main__':main()
