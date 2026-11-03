# PC 车辆检测与跟踪

核心入口为 `ethernet_tracking.py`（板卡以太网协同）与 `track_video.py`（本地视频）。使用 Ultralytics YOLOv5su、ByteTrack 及编号关联优化，默认检测车辆类别，向板端返回最多 8 个框。短时丢失可保留关联状态，持续丢失后移除，不保证任意遮挡后的编号恢复。

## 环境

`setup.ps1` 创建本目录 `.venv`，调用 `download_cuda_wheels.py` 安装所需 Windows Python 3.11 / CUDA 12.8 PyTorch 依赖，再安装 `requirements.txt`。其他平台请按对应平台准备依赖。模型 `models/yolov5su.pt` 不随源码提交，需自行从官方来源取得。

```powershell
.\setup.ps1
.\run_ethernet.ps1 -Board 192.168.2.99 -Bind 192.168.2.1
.\run_demo.ps1 -Source 'D:\videos\input.mp4'
```

`start_live.py` 可先同步电脑时钟再运行网口跟踪；`--no-sync` 跳过同步。模型加载、网口工作需要对应依赖及匹配板卡固件，不会自动烧录或更改网卡配置。

网口参数与模块职责见 [以太网说明](readme_ethernet.md)。检测结果不含真实 km/h 测速、逐目标距离或违章判断。

## 第三方来源

- Ultralytics 检测器及其集成跟踪实现：https://github.com/ultralytics/ultralytics/tree/v8.3.221
- ByteTrack 论文与原始 MIT 项目：https://github.com/FoundationVision/ByteTrack
- 官方预训练权重：https://github.com/ultralytics/assets/releases/download/v8.3.0/yolov5su.pt

依赖发行包中的许可应一并保留；Ultralytics 发行许可与 ByteTrack 原始项目许可分别适用。
