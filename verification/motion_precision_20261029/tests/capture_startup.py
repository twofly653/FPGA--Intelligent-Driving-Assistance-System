
from pathlib import Path
import sys,json,time,urllib.request
b=Path(__file__).resolve().parent.parent
root=b.parents[1];sys.path.insert(0,str(root/'dashboard'))
from server import WindowsSerial
def api(path,data=None):
 req=urllib.request.Request('http://127.0.0.1:8767'+path,None if data is None else json.dumps(data).encode(),{'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(req,timeout=6))
(b/'build/before_status.json').write_text(json.dumps(api('/api/status'),indent=2))
api('/api/disconnect',{});reader=None
try:
 reader=WindowsSerial('COM10');print('CAPTURE_READY',flush=True)
 with (b/'build/startup_uart.log').open('wb') as out:
  until=time.monotonic()+35
  while time.monotonic()<until:
   data=reader.read_chunk()
   if data:out.write(data);out.flush()
finally:
 if reader:reader.close()
 api('/api/connect',{'port':'COM10'});print('CAPTURE_DONE_RECONNECTED',flush=True)
