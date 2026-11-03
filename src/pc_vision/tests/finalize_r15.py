"""Record this offline candidate; never calls hardware or network interfaces."""
from pathlib import Path
import hashlib,json,re,sys
from datetime import datetime,timezone

PC=Path(__file__).resolve().parents[1];APP=PC.parent
OUT=PC/'validation';RUN=PC/'runs/r15_udp_final_20261030'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
protected={
 APP.parent/'deliverables/frozen_baselines/DriveSense_R14_20261030_115748_RESTORE.zip':'339b3395767d977eabb6cc3176053660e2b17721c36174e1a3cb460a1b9e5585',
 APP/'firmware/release/vehicle_assist_r14_tracking_candidate.elf':'2e5c82e4e3beca92e1f63dece80d7e869842a43e9c32abf9e05aa853089c86f2',
 APP/'firmware/release/vehicle_assist_lcd_english.elf':'f308da03d9d0feb7bed1c9057f4ef974b6378c597a6e930d929b3cbef594d36e',
}
for path,expected in protected.items():assert sha(path)==expected,('Protected baseline changed',str(path))
python_log=(OUT/'r15_final_python.log').read_text(encoding='utf-8-sig')
assert re.search(r'Ran 33 tests',python_log) and re.search(r'\nOK\s*$',python_log)
c_contracts=json.loads((OUT/'r15_network/c_contracts.json').read_text())
assert len(c_contracts)==4 and all(c['passed'] for c in c_contracts)
assert 'CHECKS=132' in (OUT/'r15_network/protocol.log').read_text()
for log in ('r15_arm_build_final.log','r15_arm_legacy_build.log'):
 assert 'ARM ELF verified:' in (OUT/log).read_text(encoding='utf-8-sig')
syntax=(OUT/'r15_rtl/final_syntax.log').read_text(encoding='utf-8-sig')
assert 'analyzing module axi_pl_hub' in syntax and 'analyzing module camera_obstacle_overlay' in syntax and 'ERROR:' not in syntax
summary=json.loads((RUN/'summary.json').read_text())
evaluation=json.loads((RUN/'sparse_evaluation.json').read_text())
assert summary['replay_only'] and not summary['board_access'] and summary['full_output_decode_passed']
assert sha(RUN/'tracked.mp4')==summary['output_sha256']
assert evaluation['selected_boxes']==23 and evaluation['iou50_matches']==23 and evaluation['sparse_id_changes']==0
paths=[]
for directory in ('firmware/src','firmware/network','rtl'):
 paths.extend(p for p in (APP/directory).rglob('*') if p.is_file())
paths.extend(p for p in PC.glob('*.py') if p.name!='finalize_validation.py')
paths.extend(p for p in (PC/'tests').glob('*') if p.is_file())
paths.extend([APP.parent/'sim/build_software.ps1',APP.parent/'sim/build_pc_network.ps1',
 APP.parent/'sim/prepare_pc_labeled_rtl.py',APP.parent/'sim/tb_pc_labels.sv',PC/'run_ethernet.ps1',PC/'readme_ethernet.md'])
paths.extend([APP/'firmware/release/vehicle_assist_pcnet_r15_candidate.elf',
 APP/'firmware/release/vehicle_assist_r15_network_disabled_check.elf'])
manifest={'candidate':'R15 Ethernet GPU tracking, OFFLINE ONLY','created_utc':datetime.now(timezone.utc).isoformat(),
 'hardware_contacted':False,'flashed':False,'windows_settings_changed':False,
 'protected_baselines':{str(p):sha(p) for p in protected},
 'files':{str(p.relative_to(APP)):sha(p) for p in sorted(set(paths))},
 'python_tests':33,'c_protocol_checks':132,'c_service_groups':c_contracts,
 'rtl':{'syntax_passed':True,'pixel_simulation_passed':False,'synthesis_run':False,
        'blocker':'Vendor xelab GCC6.2 cannot launch: Windows invalid system DLL relocation. See elaborate.log.'},
 'replay':summary,'sparse_evaluation':evaluation}
(OUT/'r15_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report=f'''# R15 网线目标跟踪离线验证

本轮未接板卡、串口、JTAG 或 hardware server，未烧录，未修改 Windows 网卡或防火墙。
在原应用目录中增加候选代码；R14 只读恢复包及 R13/R14 原 ELF 的 SHA256 均与修改前一致。

## 已验证

- 两种 ARM 构建通过：`vehicle_assist_pcnet_r15_candidate.elf` 开启网口；`vehicle_assist_r15_network_disabled_check.elf` 关闭网口用于编译回归。两者均为候选，不覆盖正式 ELF。
- Python 33 项测试通过，涵盖字节协议、重组、乱序、CRC、缺片、旧代号、变采样间隔、新目标确认、短遮挡、八槽选择及仅 localhost 的 UDP 往返。
- C 侧协议和源帧时效守卫 132 项检查，HELLO/FRAME/RESULT 跨语言黄金向量一致。
- 真实 C 服务宿主检查通过：远端框/16 位编号打包、ACK、识别开关、快速 OFF/ON 前旧采样拒绝、暂停后旧采样拒绝、过期清框；旧本地模式行为回归通过。
- 真实交通帧服务宿主检查通过：新 peek 不重复采集、不消费帧；旧 take 仍只消费一次，冻结代号、复制切片、JPEG 切换及超时行为保留。
- OB02 两个候选 Verilog-2001 模块 xvlog 语法检查通过。**像素仿真没有通过运行阶段**：xelab 附带 GCC6.2 无法启动，Windows 报系统 DLL 重定位错误。未进行综合/布线/时序验证，无新 BIT/XSA。

## 用户视频的协议回放与 GPU 跟踪

- 原视频 SHA256：`{summary['source_sha256']}`。
- 原视频按模拟 10Hz 采样，320x240 RGB565，真实分片协议经过乱序、重复、确定性缺片和坏 CRC 注入；不使用网卡，不接开发板。
- {summary['processed_frames']} 个采样时刻，{summary['reassembly']['complete']} 帧重组完成，{summary['frames_without_complete_payload']} 帧丢弃；视频中明确显示模拟丢帧，不用旧框填充。
- 使用本机 RTX 5060 Laptop GPU、YOLOv5su COCO 和采集时间驱动的 ByteTrack；非 TomMao PSP 模型。
- 检测与跟踪计算中位 {summary['compute_ms_median']:.2f}ms，P95 {summary['compute_ms_p95']:.2f}ms。**这不是网线或整机端到端延迟。**
- 既有稀疏标注 23 个目标框中，23 个达到 IoU0.5，平均匹配 IoU {evaluation['mean_best_matched_iou']:.3f}。
- 4 个既有身份标签的目标在各自标注时刻均保持同一编号；并非全片身份完整标注，不推算 IDF1、整体准确率或零换号保证。
- 同一轨迹相邻样本的原始类别变化 {summary['raw_label_changes']} 次，加权投票后变化 {summary['smoothed_label_changes']} 次；这是稳定性计数，不代表分类准确率提高到某个百分比。
- 电脑绘制全部可见轨迹，回传硬件最多 8 框。小框仅显示编号以减少文字遮挡，完整类别保存在逐帧 JSON。

演示：`../runs/r15_udp_final_20261030/tracked.mp4`；截图：同目录 `contact_sheet.jpg`；坐标/编号：`tracks.jsonl`。

## 仍需后续板上验收

实际 PHY 协商、传输吞吐、摄像头取样频率、采集到 HDMI 的时延、目标交叉/长遮挡以及 ToF/姿态/触摸/拍照并行均未实测。
网口目前传 320x240 分析帧，HDMI 保持 640x480；放大显示不等于提高采集分辨率。
现有 OB01 硬件只有坐标框；编号文字须另行完成 OB02 仿真和同轮 BIT/XSA/PS7 硬件构建，再按用户新指令烧录。

所有源文件和候选 ELF 哈希、完整结果见 `r15_manifest.json`。使用说明见 `../readme_ethernet.md`。
'''
(OUT/'r15_ethernet_tracking_validation.md').write_text(report,encoding='utf-8')
print('R15_OFFLINE_MANIFEST_PASS: protected baselines unchanged; ARM + C + Python + GPU replay recorded; no hardware contact.')
