"""Bounded UART capture for the authorized R13 download; no control writes."""
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dashboard"))
from server import WindowsSerial

port = WindowsSerial("COM10")
print("UART_CAPTURE_READY COM10 115200", flush=True)
try:
    end = time.monotonic() + 45
    with Path(__file__).with_name("startup_uart.log").open("wb") as stream:
        while time.monotonic() < end:
            data = port.read_chunk()
            if data:
                stream.write(data)
                stream.flush()
finally:
    port.close()
print("UART_CAPTURE_FINISHED", flush=True)
