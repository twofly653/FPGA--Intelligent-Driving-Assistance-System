"""Push the PC clock into the board so the LCD shows a live LOCAL TIME.

Why this exists
---------------
`ui_clock_poll()` in the firmware is a deliberate no-op: the wall clock is
recomputed from `g_anchor_ticks` every time the UI reads it, so one accepted
`TIME SET` line keeps the display ticking forever. The catch is that the board
starts with `g_valid == 0` after every power cycle *and after every JTAG RAM
reload*, and nothing on the PC side ever sent that line -- which is exactly why
the home page sat on `--:--:--` / `WAITING FOR PC SYNC`.

The USB-UART bridge also re-enumerates with a different COM number between
sessions (COM10 -> COM5 has already happened once), so the port must be
discovered by handshake instead of being hard-coded.

Wire format (firmware `ui_clock_command`, strict):
    28 characters exactly: "TIME SET YYYY-MM-DDTHH:MM:SS"
    '\\r' is ignored, '\\n' commits the line.

Usage
-----
    python auto_time_sync.py                 # auto-detect port, sync now
    python auto_time_sync.py --port COM5     # skip discovery
    python auto_time_sync.py --at 2026-11-01T13:20:00
    python auto_time_sync.py --dry-run       # probe only, send nothing
"""
from __future__ import annotations

import argparse
import datetime as dt
import sys
import time

LINE_PREFIXES = ('PHOTO ', 'TEL ', 'STAT ', 'CAMERA ', 'OBSTACLE ',
                 'CMP ', 'TRAFFIC ', 'DETECT ')
BAUD = 115200


def _list_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    return [(p.device, p.description or '') for p in list_ports.comports()]


def _read_lines(ser, deadline):
    """Drain the UART until `deadline`, returning the non-empty lines seen."""
    buf = b''
    while time.perf_counter() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
        else:
            time.sleep(0.02)
    text = buf.decode('ascii', 'replace')
    out = []
    for raw in text.replace('\r', '\n').split('\n'):
        raw = raw.strip()
        if raw:
            out.append(raw)
    return out


def probe_port(device, baud=BAUD, timeout=1.2):
    """Return the board's reply lines if `device` looks like the DriveSense UART.

    `PHOTO STATUS` is the probe of choice because it is free of side effects and
    prints `PHOTO IDLE` / `PHOTO BUSY` / `PHOTO READY` straight back.
    """
    try:
        import serial
    except ImportError:
        raise SystemExit('pyserial is missing: python -m pip install pyserial')
    try:
        ser = serial.Serial()
        ser.port = device
        ser.baudrate = baud
        ser.bytesize = serial.EIGHTBITS
        ser.parity = serial.PARITY_NONE
        ser.stopbits = serial.STOPBITS_ONE
        ser.timeout = 0.05
        ser.write_timeout = 1.0
        ser.dsrdtr = False
        ser.rtscts = False
        ser.open()
    except Exception as exc:                      # busy port, missing device, ...
        return None, str(exc)
    try:
        ser.reset_input_buffer()
        ser.write(b'PHOTO STATUS\n')
        ser.flush()
        lines = _read_lines(ser, time.perf_counter() + timeout)
    finally:
        ser.close()
    board = [ln for ln in lines if ln.startswith(LINE_PREFIXES)]
    if not board:
        return None, 'no board telemetry'
    return board, ''


def find_board(timeout=1.2):
    """Probe every COM port and return the first one that answers like the board."""
    tried = []
    for device, desc in _list_ports():
        lines, err = probe_port(device, timeout=timeout)
        if lines:
            return device, desc, lines, tried
        tried.append('%s (%s): %s' % (device, desc or '?', err))
    return None, '', [], tried


def send_time(device, when=None, baud=BAUD):
    """Send the 28-character `TIME SET` line and report the board's reply."""
    try:
        import serial
    except ImportError:
        raise SystemExit('pyserial is missing: python -m pip install pyserial')
    when = when or dt.datetime.now()
    line = 'TIME SET ' + when.strftime('%Y-%m-%dT%H:%M:%S')
    if len(line) != 28:
        raise SystemExit('refusing to send %d-char line (firmware wants 28): %r'
                         % (len(line), line))
    ser = serial.Serial()
    ser.port = device
    ser.baudrate = baud
    ser.timeout = 0.05
    ser.write_timeout = 1.0
    ser.dsrdtr = False
    ser.rtscts = False
    ser.open()
    try:
        ser.reset_input_buffer()
        ser.write((line + '\n').encode('ascii'))
        ser.flush()
        reply = _read_lines(ser, time.perf_counter() + 0.9)
    finally:
        ser.close()
    return line, reply


def sync(port='auto', when=None, dry_run=False, quiet=False):
    """Discover the board (unless `port` is given) and push the clock once."""
    log = (lambda *a: None) if quiet else print

    if port and port.lower() != 'auto':
        lines, err = probe_port(port)
        if not lines:
            log('[time] %s did not answer (%s)' % (port, err))
            return False
        device, desc = port, ''
        log('[time] %s answered as a board' % device)
    else:
        device, desc, lines, tried = find_board()
        if not device:
            log('[time] no board UART found; probes: %s' % ('; '.join(tried) or 'none'))
            return False
        log('[time] board UART = %s%s  (%s)'
            % (device, (' / ' + desc) if desc else '', lines[0]))

    if dry_run:
        log('[time] dry-run: would send "TIME SET %s"'
            % dt.datetime.now().strftime('%Y-%m-%dT%H:%M:%S'))
        return True

    line, reply = send_time(device, when)
    log('[time] sent %d chars: %s' % (len(line), line))
    tel = [ln for ln in reply if ln.startswith('TEL ')]
    if tel:
        log('[time] board still alive: %s' % tel[-1])
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description='Push the PC clock into the board over UART.')
    ap.add_argument('--port', default='auto', help="'auto' or an explicit port such as COM5")
    ap.add_argument('--at', default=None, help='explicit timestamp YYYY-MM-DDTHH:MM:SS')
    ap.add_argument('--dry-run', action='store_true', help='probe only, send nothing')
    args = ap.parse_args(argv)

    when = None
    if args.at:
        when = dt.datetime.strptime(args.at, '%Y-%m-%dT%H:%M:%S')
    ok = sync(port=args.port, when=when, dry_run=args.dry_run)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
