from datetime import datetime
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parent
APP=ROOT.parent

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

report=[]
test_log=(ROOT/'validation/lifecycle.log').read_text(encoding='utf-8-sig',errors='replace')
assert 'Ran 8 tests' in test_log and '\nOK' in test_log, 'Lifecycle test evidence missing'
for name in ('bilibili_gpu_20261030','supplied_gpu_20261030'):
    folder=ROOT/'runs'/name
    summary=json.loads((folder/'summary.json').read_text(encoding='utf-8'))
    sparse=json.loads((folder/'sparse_evaluation.json').read_text(encoding='utf-8'))
    rows=[json.loads(x) for x in (folder/'tracks.jsonl').read_text(encoding='utf-8').splitlines()]
    assert len(rows)==summary['processed_frames']
    assert summary['full_output_decode_passed'] and not summary['hardware_synchronized']
    assert sha(folder/'tracked.mp4')==summary['output_sha256']
    for i,row in enumerate(rows):
        assert row['frame_index']==summary['start_frame']+i
        assert not row['hardware_synchronized'] and row['source']=='PC_GPU_VIDEO_FILE'
        assert len({o['id'] for o in row['objects']})==len(row['objects'])
        for obj in row['objects']:
            x1,y1,x2,y2=obj['bbox_xyxy']
            assert 0<=x1<x2<summary['source_width'] and 0<=y1<y2<summary['source_height']
            assert obj['observed'] and 0<=obj['confidence']<=1 and obj['class_id'] in summary['classes']
    report.append({'name':name,'summary':summary,'sparse':sparse,
                   'video':str(folder/'tracked.mp4'),'jsonl_sha256':sha(folder/'tracks.jsonl')})

original=json.loads((APP.parent/'sim/tracking_r14_20261030/release_manifest.json').read_text())
for item in original['files']:
    assert sha(APP/item['path'])==item['sha256'],item['path']
baseline=APP.parent/'deliverables/frozen_baselines/DriveSense_R14_20261030_115748_RESTORE.zip'
assert sha(baseline)=='339b3395767d977eabb6cc3176053660e2b17721c36174e1a3cb460a1b9e5585'
manifest={'created_local':datetime.now().astimezone().isoformat(),
          'stage':'PC CUDA original-video validation only', 'pynq_access':False,
          'firmware_or_bitstream_changes':False,'real_camera_integration':False,
          'tommao_psp_weights_available':False,'global_python_modified':False,
          'user_camera_link_status':'No Ethernet cable or USB HDMI capture card; user requested video-only review first.',
          'r14_source_elf_and_readonly_recovery_zip_unchanged':True,
          'gpu':json.loads((ROOT/'validation/gpu_environment.json').read_text()),
          'lifecycle_tests_passed':8,'runs':report,
          'code_hashes':{str(p.relative_to(ROOT)):sha(p) for p in sorted(ROOT.iterdir())
                         if p.is_file() and p.suffix in ('.py','.ps1','.txt','.md')},
          'sources':['https://github.com/TomMao23/multiyolov5',
                     'https://raw.githubusercontent.com/TomMao23/multiyolov5/TomMao-2.1/README.md',
                     'https://raw.githubusercontent.com/TomMao23/multiyolov5/BS2021/detect.py',
                     'https://docs.ultralytics.com/models/yolov5/',
                     'https://docs.ultralytics.com/modes/track/',
                     'https://github.com/FoundationVision/ByteTrack']}
(ROOT/'validation/manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')

lines=['# 电脑 GPU 车辆检测与持续编号：首轮实测', '',
       '2026-10-30，按用户“允许电脑 GPU 协同，先验证效果”的选择完成。', '',
       '已运行 NVIDIA GeForce RTX 5060 Laptop GPU，PyTorch 2.9.1 + CUDA 12.8；实际 FP16 GPU 矩阵计算通过，未回退 CPU。',
       '使用官方 YOLOv5su COCO 权重与 ByteTrack，960 像素推理。不是 TomMao PSP/Lab 多任务权重的复现；未启用语义分割。', '',
       '| 原始视频 | 处理帧数 | 算法中位 / P95 | 文件吞吐（含绘制编码） | 选取标注 IoU≥0.5 |',
       '| --- | ---: | ---: | ---: | ---: |']
for row in report:
    s,g=row['summary'],row['sparse']
    lines.append(f"| {row['name']} | {s['processed_frames']} | {s['compute_ms_median']:.2f} / {s['compute_ms_p95']:.2f} ms | {s['wall_processing_fps']:.2f} 帧/秒 | {g['iou50_matches']} / {g['selected_boxes']} |")
lines += ['', '算法耗时不含读帧/绘制/编码；暖机独立执行。文件吞吐不是摄像头到显示器端到端性能。两段输出均已全片解码校验，逐帧坐标/编号及源文件、模型和结果哈希已保存。', '',
          '原用户视频既有稀疏标签中的 A、B、C、D 四个目标，跨所选标注时刻均保持原编号，稀疏 ID 变化为 0；该结论仅覆盖这些标注，不代表全片零换号。',
          '8 项生命周期检查通过，包括持续移动、停下仍可见、短暂丢失后恢复、持续丢失后过期、多个目标独立编号和低置信观测续跟。缺少当前检测时不绘制预测幽灵框。', '',
          '## 观察到的限制', '',
          '- 从截图可见部分小客车/厢式车/公交出现车型误判，远处目标标签也较密；通用 COCO 权重并非针对该俯视道路场景训练。',
          '- 没有整段穷尽标注，不能给出整体准确率、召回率或 IDF1。与 R14 的输入分辨率、检测类型不同，表中结果不是严格控制变量的算法 A/B。',
          '- 当前使用电脑上的原始视频文件，不是通过 OV5640 实时采集；结果未返回 PYNQ HDMI，也没有混入网页的真实板卡遥测。',
          '- 未实现真实 km/h 测速、过线计数、违法判断或截图中的语义分割能力。', '',
          '## 硬件后续接入', '',
          '本次 Windows 设备清单仅看到内置摄像头，以太网为未连接。需用户提供网线或 HDMI 采集链路，再开发传图、时间戳/帧号对齐、结果返回及编号文字叠加。现有串口 115200 仅传数值，当前 HDL 坐标表最多 8 框。不能将本次演示称为 PYNQ-Z2 独立 YOLO 实时运行。', '',
          '原 R14 源码、ELF 和只读恢复 ZIP 哈希保持一致。本轮未连接 PYNQ/JTAG/串口、未下载或停止原网页服务。GPU 依赖安装在 pc_vision/.venv，未修改系统显卡驱动或全局 Python。', '',
          '运行入口：pc_vision/run_demo.ps1；详细文件与来源见 pc_vision/README.md 和 validation/manifest.json。']
(ROOT/'validation/gpu_tracking_initial_validation.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(json.dumps({'videos_verified':len(report),'frames_verified':sum(r['summary']['processed_frames'] for r in report),
                  'r14_unchanged':True,'report':str(ROOT/'validation/gpu_tracking_initial_validation.md')},ensure_ascii=False))
