"""Launch the integrated UART bridge with the original DriveSense interface."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path


bridge = Path(__file__).resolve().parent.parent / "server.py"
sys.path.insert(0, str(bridge.parent))
sys.argv = [str(bridge), "--open", *sys.argv[1:]]
runpy.run_path(str(bridge), run_name="__main__")
