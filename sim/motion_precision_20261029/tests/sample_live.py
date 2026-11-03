
import json,time,sys,urllib.request
from pathlib import Path
b=Path(__file__).resolve().parent.parent
samples=[];until=time.monotonic()+float(sys.argv[2] if len(sys.argv)>2 else 15)
while time.monotonic()<until:
 begin=time.monotonic();s=json.load(urllib.request.urlopen('http://127.0.0.1:8767/api/status',timeout=4))
 samples.append({'host_time':time.time(),**s});time.sleep(max(0,.12-(time.monotonic()-begin)))
(b/'build'/(sys.argv[1]+'.json')).write_text(json.dumps(samples,indent=2))
obs={s['obstacles']['result_seq']:s['obstacles'] for s in samples if s.get('obstacles') and not s['obstacles']['stale']}
print('SAMPLE',sys.argv[1],'unique',len(obs),'count_hist',{n:sum(o['count']==n for o in obs.values()) for n in range(9)},'proc_ms',sorted(set(o['proc_ms'] for o in obs.values())))
