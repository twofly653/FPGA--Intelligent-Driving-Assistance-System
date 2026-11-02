"""One command that brings the live link up correctly.

Order matters: the UART clock must be pushed *before* the UDP pipeline starts,
because the home page is only repainted while `g_valid` is set and a JTAG RAM
reload clears it. Doing it here means nobody has to remember `TIME SET` after a
reflash.

    python start_live.py                     # sync clock, then tracking
    python start_live.py --tracker precision # forwarded to ethernet_tracking.py
    python start_live.py --no-sync           # skip the clock step
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main(argv=None):
    ap = argparse.ArgumentParser(description='Sync board clock, then start the live pipeline.')
    ap.add_argument('--no-sync', action='store_true', help='skip the UART clock push')
    ap.add_argument('--port', default='auto', help="serial port for the clock push ('auto')")
    known, forwarded = ap.parse_known_args(argv)

    if not known.no_sync:
        sys.path.insert(0, HERE)
        import auto_time_sync
        auto_time_sync.sync(port=known.port)
        print('')      # keep the pipeline banner separated from the sync output

    cmd = [sys.executable, '-u', os.path.join(HERE, 'ethernet_tracking.py')] + forwarded
    print('[launch] %s' % ' '.join(cmd[1:]))
    try:
        return subprocess.call(cmd, cwd=HERE)
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
