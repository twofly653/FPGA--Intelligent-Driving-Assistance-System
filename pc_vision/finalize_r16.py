"""Summarize existing offline evidence; never invoke a board or network tool."""
from pathlib import Path
import hashlib,json
from track_video import ROOT,cv2

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as file:
        for chunk in iter(lambda:file.read(4*1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def main():
    app=ROOT.parent;runs=ROOT/'runs/r16_release_20261030';second=ROOT/'runs/r16_release_holdout_20261030'
    a=json.loads((runs/'comparison.json').read_text());b=json.loads((second/'comparison.json').read_text())
    tests=json.loads((ROOT/'validation/r16_offline_checks.json').read_text());assert tests['tests']==44 and not tests['errors'] and not tests['failures']
    protected={
        app.parent/'deliverables/frozen_baselines/DriveSense_R14_20261030_115748_RESTORE.zip':'339b3395767d977eabb6cc3176053660e2b17721c36174e1a3cb460a1b9e5585',
        app/'firmware/release/vehicle_assist_r14_tracking_candidate.elf':'2e5c82e4e3beca92e1f63dece80d7e869842a43e9c32abf9e05aa853089c86f2',
        app/'firmware/release/vehicle_assist_lcd_english.elf':'f308da03d9d0feb7bed1c9057f4ef974b6378c597a6e930d929b3cbef594d36e'}
    hashes={str(p):digest(p) for p in protected};assert all(hashes[str(p)]==expected for p,expected in protected.items())
    # Compare the candidate ELF to the pre-change archive, too.
    import zipfile
    archive=ROOT/'validation/r15_before_quality_20261030.zip'
    with zipfile.ZipFile(archive) as z:
        member=next(n for n in z.namelist() if n.endswith('vehicle_assist_pcnet_r15_candidate.elf'))
        expected=hashlib.sha256(z.read(member)).hexdigest()
    candidate=app/'firmware/release/vehicle_assist_pcnet_r15_candidate.elf';assert digest(candidate)==expected;hashes[str(candidate)]=expected
    files=[ROOT/n for n in ('precision_tracking.py','quality_input.py','latest_artifacts.py','ethernet_tracking.py','run_ethernet.ps1','benchmark_quality.py','benchmark_association.py','render_quality.py','check_offline_r16.py','test_precision_quality.py','test_latest_artifacts.py','README_ETHERNET.md')]
    files += [app/'rtl/camera_analysis_buffer.v',app/'verification/tb_analysis_antialias.v',app/'verification/prepare_antialias_rtl.py']
    media=[]
    for file in runs.glob('*/tracked.mp4'):
        cap=cv2.VideoCapture(str(file));assert cap.isOpened()
        media.append({'path':str(file),'width':int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)),'height':int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)),'fps':cap.get(cv2.CAP_PROP_FPS),'frames':int(cap.get(cv2.CAP_PROP_FRAME_COUNT)),'sha256':digest(file)})
        cap.release()
    manifest={'revision':'R16_OFFLINE','hardware_contacted':False,'flashed':False,'physical_network_tested':False,
              'board_sender_resolution':[320,240],'native640':'offline experiment only; no board producer',
              'source_hashes':{str(p):digest(p) for p in files},'preserved':hashes,'r15_snapshot':{'path':str(archive),'sha256':digest(archive)},
              'tests':tests,'runs':a,'cross_video_runs':b,'videos':media,
              'rtl':{'syntax':'passed','static_elaboration':'passed','pixel_simulation':'blocked at xelab generated C compilation','synthesis':'not run','timing':'not run'}}
    (ROOT/'validation/r16_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    def select(items,profile,kind='precision',hz=10,size=640):return next(x for x in items if x['profile']==profile and x['tracker']==kind and x['source_fps']==hz and x['imgsz']==size)
    old=select(a,'legacy320','baseline');aa=select(a,'antialias320');vga=select(a,'native640');vga15=select(a,'native640',hz=15)
    report='''# 画质与目标跟踪 R16 离线验证

2026-10-30。本轮没有网线，没有调用串口、JTAG、hardware server、主板网口或烧录程序。使用已存储的两段视频与本地 RTX 5060 Laptop GPU。

## 完成的改动

1. 检测结果中 car / bus / truck 的 IoU > 0.8 重复框先去重，防止同一车辆被两个类别重复编号。普通重叠和摩托车保持独立。
2. 延续按采集时间推进的 ByteTrack、短时遮挡关联与类别投票；仅小幅抖动时平滑框，快速移动和重新出现时贴近当前观测。漏检时不伪造预测框。
3. 磁盘预览输出移到单独线程，队列最多一个待写帧；新帧替换等待中的旧帧，避免写文件让检测队列越来越长。保留 baseline 回退选项。
4. 写入独立抗锯齿 RTL 候选：先平均 2×2 RGB565 的三个独立分量，再降到 320×240，减少隔点采样造成的细节闪烁。Python 位精确参考验证舍入、饱和范围及通道不串扰。
5. 对比原生 640×480 本地输入和检测尺寸 640 / 960。高分辨率保留更多图像细节，但加大检测尺寸并非所有场景都更准确，因此默认仍用 imgsz=640。

颜色直方图外观关联也做了消融：权重 0.18 在第二段视频的抗锯齿输入下把选定匹配从 21/28 降至 19/28，默认已关闭。未把只在单个片段有效的设置作为默认。

## 同条件对比：10Hz 取样、相同损包时刻、RGB565

第一段为用户原视频，23 个既有稀疏标注；第二段为另一段交通视频，28 个既有稀疏标注。达标指 IoU >= 0.5。不是全片准确率、召回率或 IDF1。第二段没有跨时刻人工身份标签，不能据此声明它没有换号。

|输入 / 跟踪|用户视频达标|用户视频平均 IoU|另一视频达标|另一视频平均 IoU|
|---|---:|---:|---:|---:|
'''
    for profile,kind,label in [('legacy320','baseline','旧隔点 320×240 / 原关联'),('antialias320','precision','抗锯齿 320×240 / R16'),('native640','precision','原生 640×480 本地实验 / R16')]:
        x=select(a,profile,kind);y=select(b,profile,kind)
        report+=f"|{label}|{x['sparse_iou50']}/23|{x['sparse_mean_iou']:.3f}|{y['sparse_iou50']}/28|{y['sparse_mean_iou']:.3f}|\n"
    report+=f'''
用户视频的 4 个选定目标在各自稀疏标注时刻保持编号；这不代表全片所有车辆都不会换号。
独立 15Hz VGA 实验中，原关联的匹配结果出现 1 次选定编号变化，R16 去重后为 0；检测达标均为 23/23。
图中仍能看到 bus/car 等类别混淆，也存在小车漏检，尚未达到所有车辆精确识别。

## 计算与流畅性

- 用户视频抗锯齿输入：GPU 检测＋R16 跟踪中位 {aa['compute_ms_median']:.2f}ms，P95 {aa['compute_ms_p95']:.2f}ms。
- 原生 VGA 15Hz 实验：中位 {vga15['compute_ms_median']:.2f}ms，P95 {vga15['compute_ms_p95']:.2f}ms。
- 10Hz 为离线取样设置；15Hz 为附加实验设置，没有通过补帧伪造更高帧率。这些数值**不含真实 ARM 采集、网口传输、显示器延迟**。
- 各配置按相同序号模拟缺片、CRC 错误、重复片、乱序；但不同 Hz 的损包发生在不同视频时刻，不能横向比较其完整标注命中数。
  例如另一视频 15Hz 在 2 秒恰逢缺片，会丢掉该时刻的 6 个标注结果，因此质量主表统一使用 10Hz。
- 当前 320×240 RGB565、10Hz 的 UDP/IP 理论负荷约 {aa['nominal_udp_ip_mbps']:.2f}Mbit/s；VGA 15Hz 约 {vga15['nominal_udp_ip_mbps']:.2f}Mbit/s。
  包含协议与 UDP/IP 字节，不含以太网物理层开销，**不是实测吞吐**。

## 验证与尚未完成的部分

- 44 项 Python 回归通过，测试期间禁止创建 socket。覆盖协议、乱序/旧帧、生命周期、遮挡重关联、重复框、抖动、快速运动、颜色平均、预览丢旧保新和写入故障。
- 完整数据仍走真实分片/CRC/重组实现，再送入 GPU；没有网络套接字。用户视频 10Hz 共 {aa['processed_frames']} 个取样时刻，{aa['complete_frames']} 个完整帧，{aa['frames_without_complete_payload']} 个模拟损坏/缺失帧。
- RTL 的 xvlog 语法和 xelab 静态展开通过；生成 C 的编译失败，日志在 `r16_rtl/`。像素级仿真、综合和时序检查未通过/未执行，**此 RTL 不能视为可烧录成品**。
- **R15 板端仍只导出 320×240；native640 只存在于本地离线测试。** 安全的 VGA 帧导出与缓冲区所有权改造、PHY 协商、实际帧率和整机共存仍待完成。
- 本轮没有新 BIT/XSA/ELF，没有改变现有硬件、OLED 或摄像头寄存器。

## 查看结果

- `../runs/r16_release_20261030/comparison.png`：同一 14 秒时刻的四种显示对照。
- `../runs/r16_release_20261030/antialias320_10hz_i640_precision/tracked.mp4`：320×240 抗锯齿候选，放大显示至 640×480。
- `../runs/r16_release_20261030/native640_15hz_i640_precision/tracked.mp4`：原生 VGA 本地输入的潜力演示，未接入板端。
- 两组 `comparison.json`、`tracks.jsonl`、`sparse_evaluation.json` 保存逐帧记录和原始测量；`r16_manifest.json` 保存文件哈希和边界说明。
- 初轮探索保存在 `r16_quality_20261030`、`r16_final_20261030`、`r16_holdout_20261030`；颜色关联消融在 `r16_association_holdout_20261030`。
  最终依据以 `r16_release_20261030` 和 `r16_release_holdout_20261030` 为准。

## 保留与恢复

R14 只读恢复 ZIP、R14 跟踪 ELF、正式英文屏 ELF 均与原 SHA256 一致。
R15 候选 ELF 与本轮修改前的 `r15_before_quality_20261030.zip` 内 ELF 逐字节哈希一致。
该小型 ZIP 还保存本轮前的 PC 源码和网口源码；恢复旧 PC 逻辑也可先使用 `run_ethernet.ps1 -Tracker baseline`。
没有覆盖整套旧项目或删除旧版验证记录。

注意：R15 早期离线脚本直接用 AREA 缩到 320，与真实 RTL 的隔点采样并不相同；本轮以先模拟 VGA RGB565，再按实际隔点/新均值取样的方式重建基线，避免混用成绩。
'''
    (ROOT/'validation/画质与跟踪R16离线验证.md').write_text(report,encoding='utf-8')
    print(json.dumps({'tests':tests,'preserved_files':len(hashes),'verified_videos':len(media),'report':'validation/画质与跟踪R16离线验证.md'},ensure_ascii=False))

if __name__=='__main__':main()
