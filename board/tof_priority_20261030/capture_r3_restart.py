from pathlib import Path
import sys,time
root=Path(__file__).parents[2];sys.path.insert(0,str(root/'src'/'dashboard'))
from server import WindowsSerial
p=WindowsSerial('COM10');print('READY',flush=True)
try:
 with (Path(__file__).parent/'r3_restart_uart.log').open('wb') as f:
  end=time.monotonic()+35
  while time.monotonic()<end:
   b=p.read_chunk()
   if b:f.write(b);f.flush()
finally:p.close()
print('DONE',flush=True)
