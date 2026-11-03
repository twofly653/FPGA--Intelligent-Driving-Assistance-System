"""Bounded real-board API evidence; leaves manual detection enabled."""
import json
from pathlib import Path
import time
import urllib.request

ROOT = Path(__file__).resolve().parent
BASE = "http://127.0.0.1:8767"

def status():
    with urllib.request.urlopen(BASE + "/api/status", timeout=3) as r:
        return json.load(r)

def control(enabled):
    req = urllib.request.Request(BASE + "/api/obstacle/control",
        data=json.dumps({"enabled": enabled}).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=3) as r:
        json.load(r)
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        obj = status()
        if obj["detection"]["enabled"] == int(enabled) and not obj["detection"]["stale"]:
            return obj
        time.sleep(.1)
    raise RuntimeError(f"No board acknowledgement for enabled={enabled}")

before = status()
off = control(False)
on = control(True)
samples = []
end = time.monotonic() + 12
while time.monotonic() < end:
    samples.append(status())
    time.sleep(.2)
summary = {
    "source": "LIVE_COM10_R13", "off_ack": off["detection"],
    "off_boxes_cleared": off["obstacles"]["count"] == 0,
    "on_ack": on["detection"], "samples": len(samples),
    "connected_samples": sum(s["connected"] for s in samples),
    "fresh_camera_samples": sum(not s["camera"]["stale"] for s in samples),
    "camera_live_samples": sum(s["camera"]["live"] for s in samples),
    "fresh_detection_samples": sum(not s["detection"]["stale"] for s in samples),
    "fresh_obstacle_samples": sum(not s["obstacles"]["stale"] for s in samples),
    "unique_obstacle_results": len({s["obstacles"]["result_seq"] for s in samples}),
    "camera_sequences": [samples[0]["camera"]["sequence"], samples[-1]["camera"]["sequence"]],
    "obstacle_processing_ms": sorted({s["obstacles"]["proc_ms"] for s in samples}),
    "imu_valid_samples": sum(s["imu"] for s in samples),
    "range_states": sorted({s["range_state"] for s in samples}),
    "final_detection_enabled": samples[-1]["detection"]["enabled"],
}
(ROOT / "web_samples.json").write_text(json.dumps(samples, indent=2), encoding="utf-8")
(ROOT / "web_checks.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
(ROOT / "final_status.json").write_text(json.dumps(samples[-1], indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
