from pathlib import Path
import json,hashlib
H=Path(__file__).parent
frames={
'2':[[86,158,36,37],[142,129,20,18],[171,125,24,18],[197,144,34,33],[252,168,50,37],[172,106,14,13]],
'7':[[198,174,47,36],[141,137,20,25],[178,141,22,21],[164,113,15,16],[98,115,17,15],[68,119,19,15],[143,101,11,13]],
'13':[[17,153,42,39],[151,143,24,20],[145,121,18,15],[167,113,14,15],[185,161,40,33],[205,109,43,41],[110,99,14,13],[149,98,11,11]],
'19':[[145,165,29,39],[196,180,43,30],[92,137,23,22],[149,136,20,21],[216,138,25,21],[150,106,13,14],[168,108,14,15]]}
(H/'bilibili_sparse_labels.json').write_text(json.dumps({'description':'Manual sparse selected visible vehicle rectangles on the original, grid-only frames. Approximate +/-3 input pixels. Includes truncated vehicles; excludes distant tiny vehicles and other road users. No dense ground truth, no accuracy/recall claim. Frozen after selecting candidate, not used to tune it.','coordinates':'320x240, source active y30..209, xywh','frames':frames},indent=2))
def iou(a,b):
 x=max(a[0],b[0]);y=max(a[1],b[1]);xx=min(a[0]+a[2],b[0]+b[2]);yy=min(a[1]+a[3],b[1]+b[3]);v=max(0,xx-x)*max(0,yy-y);return v/(a[2]*a[3]+b[2]*b[3]-v)
reports=[]
for fps in (5,10):
 for variant in ('r13','r14'):
  rows=json.loads((H/f'results/bilibili_{variant}_fps{fps}.json').read_text());vals=[]
  for t,gt in frames.items():
   row=rows[round(float(t)*fps)];pairs=sorted([(iou(b,[o[k] for k in ('x','y','w','h')]),i,j) for i,b in enumerate(gt) for j,o in enumerate(row['objects'])],reverse=True);seen=set();used=set();scores={}
   for v,i,j in pairs:
    if i not in seen and j not in used:scores[i]=v;seen.add(i);used.add(j)
   vals.extend(scores.get(i,0) for i in range(len(gt)))
  result={'variant':variant,'fps':fps,'selected_boxes':len(vals),'iou30':sum(v>=.3 for v in vals),'iou50':sum(v>=.5 for v in vals),'mean_iou':sum(vals)/len(vals)};reports.append(result);print(result)
(H/'bilibili_sparse_scores.json').write_text(json.dumps(reports,indent=2))
