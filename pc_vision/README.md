# 电脑 GPU 车辆检测与编号跟踪：第一阶段视频验证

**2026-10-30 新进度：** 网线协同代码与 ARM 候选 ELF 已完成离线准备，见 [R15 网线使用说明](README_ETHERNET.md)。本文件以下保留第一阶段原生视频验证记录；R15 的传图分辨率、回传上限和未完成的板上验证以新说明为准。

本目录是用户明确同意的“电脑 GPU 与 PYNQ 协同”方案的离线验证入口。原 R14 固件、当前运行硬件和只读恢复包保持不变。这里的输出来自本地原始视频，不是开发板实时摄像头，也未同步到 HDMI 或硬件遥测。

首轮已在 RTX 5060 Laptop GPU 完成两段原始视频、共 1160 帧的处理。原用户视频的既有 23 个选取目标框中，22 个达到 IoU≥0.5；4 个有身份标签的目标在标注时刻均保持原编号。仍存在车型误判，这不是全片准确率或零换号保证。实测与限制见 [首轮报告](validation/首轮GPU车辆编号跟踪验证.md)。用户目前没有网线/HDMI 采集卡，明确选择先观看这些视频。

- [用户原视频的编号跟踪演示](runs/supplied_gpu_20261030/tracked.mp4)
- [另一段车流视频演示](runs/bilibili_gpu_20261030/tracked.mp4)

## 当前模型与行为

- 使用 Ultralytics YOLOv5su 的 COCO 预训练检测器，ByteTrack 维护跨帧编号。默认检测 car、motorcycle、bus、truck。
- 确认目标后，绿框旁显示 `ID 数字 类别`。相同轨迹继续使用原编号，停止运动的可见车辆仍由检测器支持；不要求它每帧都在运动。
- 短时丢失保留关联历史 2 秒，重现时尝试恢复原编号；漏检期间不画凭空预测的车辆框。持续丢失后删除历史，不承诺任意长遮挡后的身份恢复。
- 输出所有处理帧的原始坐标/ID/类别/时间与 JSONL，不插帧、不伪造测速、不将置信分数当准确率。
- 这是 YOLOv5u 验证基线，不是 TomMao23 的 PSP/Lab 多任务模型。原仓库预训练权重在百度网盘，需要取得相应 `.pt` 后另外做旧版模型适配和实测；不能直接用本脚本的 Ultralytics 加载器加载其旧 YOLOv5 自定义模型。

## 启动

运行 `setup.ps1` 创建本目录内 `.venv`，安装 CUDA 12.8 PyTorch 和固定版本依赖；不会修改系统驱动或全局 Python。下载的验证权重位于 `models/yolov5su.pt`，来源及 SHA256 见同目录 `yolov5su.source.json`。

```powershell
powershell -File .\run_demo.ps1
# 指定原始视频：
powershell -File .\run_demo.ps1 -Source 'D:\path\original.mp4'
```

每次生成独立 `runs` 结果目录，包含浏览器兼容的 H.264 `tracked.mp4`、逐帧 `tracks.jsonl`、阶段截图、`summary.json`。默认强制 CUDA；CUDA 失败会报告错误，不会把 CPU 速度冒充 GPU 速度。首次暖机不计入计算时延；计算时延和文件处理吞吐不等于实时摄像头端到端延迟。

功能检查：`.venv\Scripts\python.exe -m unittest test_tracking_lifecycle -v`。这些合成输入仅验证丢失/恢复等生命周期，不代表真实交通场景准确率。实际视频效果和稀疏标注结果需单独核对。

## 第一阶段结束时的后续事项（当前进度见 R15 说明）

1. 建立 OV5640/PYNQ 向电脑传图的链路，例如网口传输或 HDMI 采集；现有 115200 串口只同步数值，不能承载当前原始视频流。摄像头和扩展板针脚不因此自动变更。
2. 对齐帧编号和时间戳，将电脑得到的目标结果返回开发板；增加失效、暂停和识别开关处理。
3. 扩展 HDMI 叠加层：当前最多 8 个框，现有硬件寄存器只接收框坐标，未接收目标编号/车型文字。图中十几个目标和编号文字需更新 RTL/协议，不能只改网页。
4. 实测整机延迟、传图速度、遮挡/交叉目标编号、交通识别与传感器是否受影响。只有完成后才可称为硬件实时集成。

本轮没有提供真实 km/h 测速、上下行统计或违章识别。测速需要相机/道路尺度标定；这些不由一个车辆检测模型直接获得。

## 原始资料与许可

- TomMao23/multiyolov5：BS2021 / TomMao-2.1，目标检测和语义分割，GPL-3.0：https://github.com/TomMao23/multiyolov5
- 本轮验证检测器与集成 ByteTrack 实现：Ultralytics 8.3.221，依赖发行包内保留其许可证：https://github.com/ultralytics/ultralytics/tree/v8.3.221
- ByteTrack 原始论文和 MIT 项目：https://github.com/FoundationVision/ByteTrack
- 预训练验证权重：https://github.com/ultralytics/assets/releases/download/v8.3.0/yolov5su.pt

与原项目区分：https://docs.ultralytics.com/models/yolov5/ 明确说明新版 YOLOv5u 和旧版 YOLOv5 权重不互通。
