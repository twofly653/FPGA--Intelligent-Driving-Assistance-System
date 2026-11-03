from pathlib import Path
import sys,time,json,urllib.request
root=Path(__file__).parents[1]
sys.path.insert(0,str(root/'src'/'dashboard'))
from server import WindowsSerial

def api(path,data):
 req=urllib.request.Request('http://127.0.0.1:8767'+path,json.dumps(data).encode(),{'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(req,timeout=5))
api('/api/disconnect',{})
reader=None;buf=bytearray()
try:
 reader=WindowsSerial('COM10');until=time.monotonic()+6
 while time.monotonic()<until:buf.extend(reader.read_chunk())
finally:
 if reader:reader.close()
 api('/api/connect',{'port':'COM10'})
(root/'board/sensor_diagnostic_uart.log').write_bytes(buf)
for line in buf.decode('ascii','replace').splitlines():
 if line.startswith('STAT '): print(line)
