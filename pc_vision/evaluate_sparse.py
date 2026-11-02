"""Compare to pre-existing selected labels; never claim whole-scene accuracy."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
APP = ROOT.parent

def iou(a, b):
    area_a = max(0, a[2]-a[0]) * max(0, a[3]-a[1])
    area_b = max(0, b[2]-b[0]) * max(0, b[3]-b[1])
    overlap = max(0, min(a[2], b[2])-max(a[0], b[0])) * max(0, min(a[3], b[3])-max(a[1], b[1]))
    return overlap/(area_a+area_b-overlap) if area_a+area_b>overlap else 0

def evaluate(run, source):
    labelpath = APP / ('verification/vehicle_tracking_video_20261029/sparse_labels.json' if source=='supplied'
                       else 'verification/tracking_r14_20261030/bilibili_sparse_labels.json')
    reference = json.loads(labelpath.read_text())
    summary=json.loads((run/'summary.json').read_text(encoding='utf-8'))
    rows = [json.loads(line) for line in (run/'tracks.jsonl').read_text().splitlines()]
    padding_y = 47 if source=='supplied' else 30
    if summary.get('rgb565_wire'):padding_y=0
    measurements, histories = [], {}
    for timestamp, gt in reference['frames'].items():
        observed = min(rows, key=lambda row: abs(row['time_s']-float(timestamp)))
        assert abs(observed['time_s']-float(timestamp)) < .06
        if isinstance(gt, list):
            gt = {str(k): b for k,b in enumerate(gt)}
        # Original existing labels use active 320-wide video with black bars.
        converted = []
        for obj in observed['objects']:
            x1,y1,x2,y2 = obj['bbox_xyxy']
            scale = 320/observed['width']
            converted.append([x1*scale,y1*scale+padding_y,x2*scale,y2*scale+padding_y])
        boxes = {k:[x,y,x+w,y+h] for k,(x,y,w,h) in gt.items()}
        pairs = sorted([(iou(b,det),k,j) for k,b in boxes.items() for j,det in enumerate(converted)], reverse=True)
        matched, used = {}, set()
        for overlap,k,j in pairs:
            if k not in matched and j not in used:
                matched[k] = (overlap,j)
                used.add(j)
        for key in gt:
            overlap, j = matched.get(key,(0,None))
            tid = observed['objects'][j]['id'] if j is not None else None
            measurements.append({'time_s':float(timestamp),'label':key,'iou':overlap,
                                 'track_id':tid, 'matched_iou50':overlap>=.5})
            if source=='supplied' and overlap>=.3:
                histories.setdefault(key,[]).append({'time_s':float(timestamp),'id':tid})
    scores=[x['iou'] for x in measurements]
    result={'source':source,'run':str(run),'existing_labels':str(labelpath),
            'selected_boxes':len(scores),'iou50_matches':sum(v>=.5 for v in scores),
            'iou30_matches':sum(v>=.3 for v in scores),'mean_best_matched_iou':sum(scores)/len(scores),
            'sparse_identity_histories':histories,
            'sparse_id_changes':sum(sum(a['id']!=b['id'] for a,b in zip(h,h[1:])) for h in histories.values()),
            'measurements':measurements,
            'limits':'Pre-existing sparse selected boxes, not exhaustive GT; no overall precision/recall/IDF1. Check run metadata for native/letterboxed input, model size and hardware slot selection.'}
    (run/'sparse_evaluation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('measurements',)},ensure_ascii=False,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--run',type=Path,required=True)
    parser.add_argument('--source',choices=['bilibili','supplied'],required=True)
    args=parser.parse_args()
    evaluate(args.run,args.source)
