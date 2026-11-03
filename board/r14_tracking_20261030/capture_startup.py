from pathlib import Path
import json
import sys
import time
import urllib.request

D = Path(__file__).parent
P = D.parents[1]
sys.path.insert(0, str(P / 'dashboard'))
from server import WindowsSerial

def post(path, data):
    req = urllib.request.Request(
        'http://127.0.0.1:8767' + path, json.dumps(data).encode(),
        {'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(req, timeout=5))

post('/api/disconnect', {})
reader = None
try:
    reader = WindowsSerial('COM10')
    print('CAPTURE_READY', flush=True)
    with (D / 'startup_uart.log').open('wb') as out:
        end = time.monotonic() + 35
        while time.monotonic() < end:
            data = reader.read_chunk()
            if data:
                out.write(data)
                out.flush()
finally:
    if reader:
        reader.close()
    post('/api/connect', {'port': 'COM10'})
print('CAPTURE_COMPLETE_RECONNECTED', flush=True)
