from pathlib import Path
import json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];APP=ROOT.parent;SRC=APP/'firmware/src'
sys.path.insert(0,str(ROOT))
from net_protocol import Header,pack,HELLO,FRAME,result_packet
OUT=ROOT/'validation/r15_network';OUT.mkdir(parents=True,exist_ok=True)
GCC=Path(r'D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe')

def run(args):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True)
    if p.returncode:raise RuntimeError(p.stdout+p.stderr)
    return p.stdout

checks=[]
for name,files,flags in [
    ('protocol',['pc_net_protocol.c','pc_net_guard.c'],[]),
    ('remote_overlay',['obstacle_service.c','obstacle_motion_core.c'],['-DAPP_ENABLE_OBSTACLES=1','-DAPP_ENABLE_PC_NETWORK=1','-DOBSTACLE_SERVICE_HOST_TEST']),
    ('legacy_overlay',['obstacle_service.c'],['-DAPP_ENABLE_OBSTACLES=1','-DOBSTACLE_SERVICE_HOST_TEST']),
    ('shared_frame',['traffic_service.c'],['-DTRAFFIC_SERVICE_HOST_TEST'])]:
    test=ROOT/'tests'/('test_net_core.c' if name=='protocol' else 'test_remote_overlay.c')
    if name=='legacy_overlay':test=APP.parent/'sim/motion_precision_20261029/tests/test_obstacle_service.c'
    if name=='shared_frame':test=ROOT/'tests/test_shared_frame.c'
    exe=OUT/(name+'.exe')
    run([GCC,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',SRC,*flags,*[SRC/f for f in files],test,'-o',exe])
    output=run([exe]);(OUT/(name+'.log')).write_text(output)
    checks.append({'name':name,'passed':True})
    if name=='protocol':
        actual=dict(line.split('=',1) for line in output.splitlines() if '=' in line)
        expected={
            'HELLO':pack(Header(HELLO,0x12345678)).hex(),
            'FRAME':pack(Header(FRAME,0x12345678,4,9,123456,2,2,1,0,1,8),bytes(range(8))).hex(),
            'RESULT':result_packet(Header(FRAME,0x12345678,4,9,123456,320,240),[
                {'id':17,'bbox_xyxy':[10,20,29,39],'confidence':.901,'class_id':2}],27).hex()}
        for key,data in expected.items():assert actual[key]==data,(key,actual[key],data)
        print('C checks:',actual['CHECKS'],'; C/Python golden vectors matched')
    else:print(output.strip())
(OUT/'c_contracts.json').write_text(json.dumps(checks,indent=2))
