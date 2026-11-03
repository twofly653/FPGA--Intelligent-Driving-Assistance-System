from pathlib import Path
import sys,time
root=Path(__file__).parents[2];sys.path.insert(0,str(root/'src'/'dashboard'))
from server import WindowsSerial
p=WindowsSerial('COM10');buf=bytearray()
try:
 until=time.monotonic()+5
 while time.monotonic()<until: buf.extend(p.read_chunk())
finally:p.close()
(Path(__file__).parent/'initial_uart.log').write_bytes(buf)
lines=buf.decode('ascii','replace').splitlines();print('received_bytes',len(buf))
for line in lines[-12:]:print(line)
