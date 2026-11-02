from pathlib import Path
import hashlib,json,struct,subprocess
H=Path(__file__).parent;P=H.parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((H/'manifest.json').read_text())
assert manifest['candidate_c_sha']==sha(P/'firmware/src/obstacle_motion_core.c')
assert manifest['candidate_h_sha']==sha(P/'firmware/src/obstacle_motion_core.h')
r13=P/'firmware/release/vehicle_assist_lcd_english.elf'
assert sha(r13)=='f308da03d9d0feb7bed1c9057f4ef974b6378c597a6e930d929b3cbef594d36e'
elf=P/'firmware/release/vehicle_assist_r14_tracking_candidate.elf'
b=elf.read_bytes();assert b'CAMERA_SYNC_R14TRACK_20261030' in b
assert b'CAMERA_SYNC_R13TRACK_20261030' not in b
cmd=[r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(P/'firmware/src'),str(H/'test_extent.c'),'-o',str(H/'test_extent.exe')]
q=subprocess.run(cmd,capture_output=True,text=True);assert q.returncode==0,q.stderr
q=subprocess.run([str(H/'test_extent.exe')],capture_output=True,text=True);assert q.returncode==0,q.stderr
(H/'targeted_test.log').write_text(q.stdout)
files=[P/'firmware/src/main.c',P/'firmware/src/obstacle_motion_core.c',P/'firmware/src/obstacle_motion_core.h',P/'firmware/src/collision_service.c',P/'firmware/src/vl53l0x.c',P/'verification/build_software.ps1',elf,P/'firmware/release/vehicle_assist_r14_tracking_candidate.map',H/'evaluate.py',H/'render_comparison.py',H/'manifest.json',H/'sparse_scores.json',H/'bilibili_sparse_labels.json',H/'bilibili_sparse_scores.json',H/'contracts_manifest.json',H/'scene_regression/regression.json',H/'render_manifest.json']
record={'version':'CAMERA_SYNC_R14TRACK_20261030','programmed':False,'board_running_version':'R13; unchanged during this offline task','r13_release_preserved':True,'source_frozen_after_final_tests':True,'experimental_tof_soft_reset':False,'entry':hex(struct.unpack_from('<I',b,24)[0]),'candidate_bytes':len(b),'files':[{'path':str(p.relative_to(P)),'sha256':sha(p),'bytes':p.stat().st_size} for p in files]}
(H/'release_manifest.json').write_text(json.dumps(record,indent=2));print(q.stdout);print('Candidate SHA256:',sha(elf));print('Preserved R13 hash matches previous release')
