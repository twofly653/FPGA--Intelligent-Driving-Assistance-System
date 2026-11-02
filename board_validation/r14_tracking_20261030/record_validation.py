from pathlib import Path
from datetime import datetime
import hashlib
import json
import time
import urllib.request

D = Path(__file__).parent
P = D.parents[1]
M = json.loads((P / 'verification/tracking_r14_20261030/release_manifest.json').read_text())
for item in M['files']:
    p = P / item['path']
    assert hashlib.sha256(p.read_bytes()).hexdigest() == item['sha256'], p

log = (D / 'startup_uart.log').read_text(errors='replace')
boot = log[log.index(M['version']):]
(D / 'r14_boot_only.log').write_text(boot, encoding='utf-8')
assert 'R14_TRACKING_CANDIDATE_DOWNLOAD_COMPLETE' in (D / 'download.log').read_text(encoding='utf-8-sig', errors='replace')
assert 'LCD home up' in boot
assert 'VL53L0X ranging ready=1 result=0' in boot

samples = []
for _ in range(5):
    with urllib.request.urlopen('http://127.0.0.1:8767/api/status', timeout=5) as response:
        samples.append(json.load(response))
    time.sleep(1)
(D / 'after_download_samples.json').write_text(json.dumps(samples, ensure_ascii=False, indent=2), encoding='utf-8')
assert all(s['connected'] and not s['stale'] for s in samples)
assert all(s['camera']['live'] and s['camera']['configured'] and not s['camera']['stale'] for s in samples)
assert all(s['detection']['enabled'] and not s['detection']['stale'] for s in samples)
assert all(s['obstacles']['available'] and not s['obstacles']['stale'] for s in samples)

hardware_root = Path(r'D:\project\deliveries\Task3_Submission_20261026\github\PYNQ-Z2-Vehicle-Vision\current_release')
hardware = []
for name in ('vehicle_top.bit', 'vehicle_top.xsa', 'ps7_init.tcl'):
    p = hardware_root / name
    hardware.append({'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
result = {
    'timestamp_local': datetime.now().astimezone().isoformat(),
    'version': M['version'], 'programmed': True,
    'elf': str(P / 'firmware/release/vehicle_assist_r14_tracking_candidate.elf'),
    'elf_sha256': hashlib.sha256((P / 'firmware/release/vehicle_assist_r14_tracking_candidate.elf').read_bytes()).hexdigest(),
    'hardware': hardware, 'boot_banner_confirmed': True,
    'lcd_home_initialized': True, 'lcd_physical_display_user_confirmation': 'pending',
    'tof_initialization_ready': True, 'tof_valid_target_distance_confirmation': 'pending',
    'dashboard': 'http://127.0.0.1:8767/#camera',
    'samples': len(samples), 'uart_connected_and_fresh': True,
    'camera_live_and_configured': True, 'detection_enabled': True,
    'detector_processing_ms': [s['obstacles']['proc_ms'] for s in samples],
    'imu_ready': [s['imu'] for s in samples],
    'all_function_acceptance': 'not performed; user requested visual tracking test',
    'offline_release_manifest': str(P / 'verification/tracking_r14_20261030/release_manifest.json'),
    'formal_r13_elf_preserved': True
}
(D / 'board_manifest.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(result, ensure_ascii=False))
