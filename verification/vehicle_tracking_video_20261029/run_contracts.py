"""Offline checks of the production motion core and service; no hardware access."""
from pathlib import Path
import hashlib, json, subprocess

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
SRC = PROJECT / 'firmware/src'
TESTS = PROJECT / 'verification/motion_precision_20261029/tests'
GCC = Path(r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe')
OUT = HERE / 'build/contracts'
OUT.mkdir(parents=True, exist_ok=True)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

report = {'hardware_access': False, 'tests': []}
cases = [(name, TESTS / (name + '.c')) for name in
         ('test_obstacle_motion_core', 'test_precision_contract', 'test_obstacle_motion_adversarial')]
cases += [('test_tracking_timing', HERE / 'test_tracking_timing.c'),
          ('test_obstacle_service', TESTS / 'test_obstacle_service.c')]
for name, test in cases:
    service = name == 'test_obstacle_service'
    source = SRC / ('obstacle_service.c' if service else 'obstacle_motion_core.c')
    sources = [source, test, SRC / 'obstacle_motion_core.h']
    if service:
        sources += [SRC / 'obstacle_service.h', SRC / 'app_features.h', SRC / 'traffic_service.h']
    hashes = {str(p): sha(p) for p in sources}
    exe = OUT / (name + '.exe')
    cmd = [str(GCC), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(SRC)]
    if service:
        cmd += ['-DAPP_ENABLE_OBSTACLES=1', '-DOBSTACLE_SERVICE_HOST_TEST=1']
    cmd += [str(source), str(test), '-o', str(exe)]
    compile_result = subprocess.run(cmd, capture_output=True, text=True)
    assert compile_result.returncode == 0, compile_result.stdout + compile_result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    log = compile_result.stdout + compile_result.stderr + result.stdout + result.stderr
    (OUT / (name + '.log')).write_text(log, encoding='utf-8')
    assert all(sha(Path(p)) == h for p, h in hashes.items()), 'Sources changed during test'
    report['tests'].append({'name': name, 'command': cmd, 'source_sha256': hashes,
                            'exe_sha256': sha(exe), 'returncode': result.returncode, 'output': log})
    (OUT / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(name, log.strip(), flush=True)
    assert result.returncode == 0, log
