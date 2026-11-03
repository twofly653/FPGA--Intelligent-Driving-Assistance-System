# DriveSense：基于 PYNQ 的车载智能辅助系统

本仓库仅保留核心源码、必要的构建/运行入口和第三方许可。源码版本为 OB02 硬件接口及 R22 固件快照；此次精简不更改固件算法。历史测试、截图、日志、实验记录及无关参考材料已移除，可从 Git 历史恢复。

## 核心功能

- OV5640 摄像头采集与 HDMI 视频输出，支持原彩、Sobel 边缘、灰度和去雾模式。
- 受控场景交通灯颜色与七段倒计时识别。
- 运动区域检测、目标编号和多框显示；PC 网口协同提供车辆检测与跟踪。
- ToF 单点测距与分级预警，姿态解算及环境传感器采集。
- 英文扩展屏交互、时钟和行程计时；网页同步数据与控制状态。
- 原生 2592×1944 JPEG 静态拍照，完成后恢复预览。

## 目录

```text
src/
├── firmware/       # ARM 固件、传感器驱动和网络协议
├── rtl/            # FPGA 接口、分析缓存及叠加模块
├── dashboard/      # 网页、串口桥、三维车模与必要资源
└── pc_vision/      # PC 检测、目标跟踪和以太网收发
build/              # ARM 应用构建脚本
README.md
```

## 启动网页

在仓库根目录运行：

```powershell
python .\src\dashboard\server.py --http-port 8767
```

打开 `http://127.0.0.1:8767/`，选择实际串口并连接。姿态、摄像头状态及传感器页面分别位于 `/#vehicle`、`/#camera`、`/#sensors`；摄像头主视频继续通过 HDMI 输出。

## PC 车辆检测与跟踪

先按 [PC 端说明](src/pc_vision/README.md) 准备隔离 Python 环境、GPU 依赖和模型权重。板卡网络固件与电脑配置匹配后运行：

```powershell
cd src/pc_vision
.\run_ethernet.ps1 -Board 192.168.2.99 -Bind 192.168.2.1
```

离线处理自己的视频时显式指定输入，不附带第三方测试视频：

```powershell
.\run_demo.ps1 -Source 'D:\videos\input.mp4'
```

## 构建固件

需先提供 AMD ARM 工具链及与 OB02 硬件匹配的 standalone BSP，在仓库根目录执行：

```powershell
.\build\build_software.ps1 -ToolRoot 'D:\FPGA\2026.1' `
  -CameraControl 14 -EnableObstacles -EnableWebSync -EnableManualDetection `
  -EnablePhotoSettle -PhotoSettleMs 4000
```

PC 网络版追加 `-EnablePcNetwork -OutputName vehicle_assist_pcnet.elf`，并提供工具链对应的 lwIP 2.2 源码。输出位于 `src/firmware/release/`，脚本仅编译，不自动烧录。

本仓库不含 BIT、XSA、ELF、BSP、模型权重和完整 Vivado 工程。RTL 目录含三个接口/叠加相关源文件，不能单独重建整套 FPGA 硬件。烧录必须另行准备同一发布版本的硬件、应用及 PS 初始化文件。

## 使用范围与许可

板端独立目标检测基于固定摄像头运动区域；PC 协同模式提供类别检测与编号关联。ToF 是单点测距，不能解释为逐框距离；六轴相对航向不等于磁北。真实道路效果需另行验证。

网页渲染器、车模及素材许可见 [第三方说明](src/dashboard/drivesense/third_party.md)。PC 检测与跟踪依赖来源见 [PC 端说明](src/pc_vision/README.md)。保留依赖自带许可，不新增覆盖所有第三方代码的统一授权。
