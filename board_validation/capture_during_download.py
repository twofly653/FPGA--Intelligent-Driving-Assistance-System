from pathlib import Path
import sys,time,json,urllib.request
root=Path(__file__).parents[1];sys.path.insert(0,str(root/'dashboard'))
from server import WindowsSerial

def api(path,data):
 req=urllib.request.Request('http://127.0.0.1:8767'+path,json.dumps(data).encode(),{'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(req,timeout=5))
api('/api/disconnect',{})
reader=None
try:
 reader=WindowsSerial('COM10');print('CAPTURE_READY',flush=True)
 with (root/'board_validation/tof_recovery_startup_uart.log').open('wb') as out:
  until=time.monotonic()+28
  while time.monotonic()<until:
   b=reader.read_chunk()
   if b:out.write(b);out.flush()
finally:
 if reader:reader.close()
 api('/api/connect',{'port':'COM10'})
 print('CAPTURE_COMPLETE_RECONNECTED',flush=True)
