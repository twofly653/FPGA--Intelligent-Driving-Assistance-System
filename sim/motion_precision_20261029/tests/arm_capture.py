from pathlib import Path
import sys,json,time,urllib.request
b=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(b.parents[1]/'src'/'dashboard'))
from server import WindowsSerial
def api(path,data=None):
 req=urllib.request.Request('http://127.0.0.1:8767'+path,None if data is None else json.dumps(data).encode(),{'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(req,timeout=6))
s=api('/api/status');port=s.get('port') or 'COM10';was=s['connected'];reader=None
if was:api('/api/disconnect',{})
try:
 reader=WindowsSerial(port);reader.write(b'OB CAPTURE\n');print('CAPTURE_ARMED',flush=True)
finally:
 if reader:reader.close()
 if was:api('/api/connect',{'port':port})
print('DASHBOARD_RESTORED',flush=True)
