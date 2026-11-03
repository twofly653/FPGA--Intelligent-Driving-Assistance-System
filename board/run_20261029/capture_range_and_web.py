from pathlib import Path
import json,re,subprocess,threading,time,urllib.request,sys
base=Path(__file__).resolve().parent
label=sys.argv[1] if len(sys.argv)>1 else 'card_50_60cm'
if not re.fullmatch(r'[a-z0-9_]+',label): raise SystemExit('invalid label')
script=(base/'range_default_card.tcl').read_text(encoding='ascii').replace('START_DEFAULT_R3_CARD_20_30CM','START_DEFAULT_R3_'+label.upper())
p=base/('range_'+label+'.tcl');p.write_text(script,encoding='ascii')
rows=[];errors=[];done=threading.Event()
def api_capture():
    while not done.is_set():
        try:
            s=json.load(urllib.request.urlopen('http://127.0.0.1:8767/api/status',timeout=3))
            s['captured_monotonic']=time.monotonic();rows.append(s)
        except Exception as e: errors.append(str(e))
        done.wait(.2)
t=threading.Thread(target=api_capture);t.start()
print('SAMPLING_STARTED '+label,flush=True)
try:
    r=subprocess.run([r'D:\FPGA\2026.1\Vitis\bin\xsdb.bat',str(p)],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=35)
finally:
    done.set();t.join(timeout=5)
(base/('range_'+label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
raw=[dict(zip(('index','status','mm','signal_16_16','ambient_16_16','io'),map(int,m))) for m in re.findall(r'^SAMPLE (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)',r.stdout,re.M)]
valid=[v for v in raw if v['status']==0 and v['mm']<4000 and v['io']==0]
webvalid=[v for v in rows if v.get('range_mm',-1)>=0 and not v.get('stale',True)]
summary={'label':label,'xsdb_exit':r.returncode,'raw_observations':len(raw),'raw_valid_observations':len(valid),'raw_statuses':sorted(set(v['status'] for v in raw)),'raw_mm_min':min((v['mm'] for v in valid),default=None),'raw_mm_max':max((v['mm'] for v in valid),default=None),'web_observations':len(rows),'web_valid_observations':len(webvalid),'web_mm_min':min((v['range_mm'] for v in webvalid),default=None),'web_mm_max':max((v['range_mm'] for v in webvalid),default=None),'web_states':sorted(set(v.get('range_state','') for v in rows)),'web_levels':sorted(set(v.get('level',-1) for v in rows)),'api_errors':errors,'last_web':rows[-1] if rows else None,'raw':raw}
(base/('web_'+label+'.json')).write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
(base/('summary_'+label+'.json')).write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(r.stdout)
print(json.dumps({k:v for k,v in summary.items() if k not in ('raw','last_web')},ensure_ascii=True))
print('SAMPLING_FINISHED',flush=True)
raise SystemExit(r.returncode)
