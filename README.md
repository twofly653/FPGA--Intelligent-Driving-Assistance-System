# DriveSense 综合应用：R14 跟踪测试版在板

本目录基于保留版摄像头与扩展板系统，重排扩展屏的全英文界面：首页中央为真实 24 小时时钟和日期，下方保留行程计时，其他功能使用统一深色卡片、方块入口与明确的数据状态。

当前正式构建标识：`CAMERA_SYNC_R13TRACK_20261030`。本轮在原工程内优化固定摄像头运动目标跟踪，保留全英文扩展屏、时钟、传感器、交通识别、图像处理、网页同步、手动识别开关和 500 万像素拍照路径。

正式文件：[vehicle_assist_lcd_english.elf](firmware/release/vehicle_assist_lcd_english.elf)，1,147,852 字节；SHA256：`f308da03d9d0feb7bed1c9057f4ef974b6378c597a6e930d929b3cbef594d36e`。ARM 构建检查与本轮运动跟踪离线回归已通过；2026-10-30 已依用户授权下载，启动标识与该 ELF 相符。用户确认 HDMI 实时图像、扩展屏正常；四模式实测 16.437–16.578 fps，网页识别开关与数据通信通过。详见 [R13 本轮板测](board_validation/r13_20261030/validation.md)。车辆跟随准确性及完整功能专项验收仍需分别验证。

本轮入口：[R13 视频回归与复现](verification/vehicle_tracking_video_20261029/README.md)、[中文验证报告](verification/vehicle_tracking_video_20261029/R13车辆跟踪优化验证报告.md)。R12/R11 板测、不可变抓帧及回放证据保存在[历史跟踪记录](verification/motion_precision_20261029/README.md)。发布路径会随构建更新，核对版本应同时核对标识和 SHA256。

## 当前板上测试版：R14（2026-10-30 已下载）

用户要求保留的当前 R14 已冻结为[独立恢复包](../deliverables/frozen_baselines/DriveSense_R14_20261030_115748_RESTORE.zip)，21.03 MiB、373 个归档项，完整解压 CRC/SHA256 校验通过；ZIP SHA256 为 `339b3395767d977eabb6cc3176053660e2b17721c36174e1a3cb460a1b9e5585`。内含原始 R14 ELF、源码、网页、BSP、配套 BIT/XSA/PS7 和相对路径恢复脚本。恢复包标记只读，后续开发不覆盖此包；打包未连接或重新下载板卡。详见包内 `README_恢复说明.md`。

当前源码和板上启动标识为 `CAMERA_SYNC_R14TRACK_20261030`，独立输出 `firmware/release/vehicle_assist_r14_tracking_candidate.elf`，SHA256 为 `2e5c82e4e3beca92e1f63dece80d7e869842a43e9c32abf9e05aa853089c86f2`。默认正式 ELF 仍保留上文 R13，便于回退。R14 缩小了外观续跟的每次尺度搜索范围并加入变化代价，同时统一预测时刻的关联重叠评分；两段视频的短时框体塌缩减少，遮挡和编号连续性仍有限。对比视频、完整指标及复现步骤见[本轮离线报告](verification/tracking_r14_20261030/R14目标追踪优化验证报告.md)。

2026-10-30 依用户“烧录，我测试一下效果”授权，使用配套 BIT、XSA、PS7 初始化文件完成 JTAG RAM 下载。已确认 R14 启动标识、LCD 首页初始化、ToF 初始化成功、640×480 摄像头实时输入、网页 COM10 新鲜数据和识别开启。连续 5 次网页快照中 IMU 就绪、识别数据持续更新，目标处理耗时 36 ms；这不是端到端视频延迟。实体双屏和视频跟踪效果待用户验收，本轮没有有效目标距离或完整功能复测结论。详见[板上验证记录](board_validation/r14_tracking_20261030/validation.md)。离线发布清单仍保留构建时的“未烧录”历史状态，以独立板测清单记录本次部署。

verification/vehicle_tracking_video_20261029/build/contracts是可复用的当前源码测试输出，最新一次已验证R14源码；R13板测与发布哈希仍以board_validation/r13_20261030不可变记录为准。
## 当前功能与验证边界

2026-10-30 另完成[电脑 GPU 车辆检测与编号跟踪的首轮验证](pc_vision/README.md)：RTX 5060 Laptop GPU、YOLOv5su + ByteTrack，两段本地原始视频共 1160 帧。此为电脑视频演示，未接入 OV5640 实时图像或返回 PYNQ HDMI；用户目前没有网线或采集卡，先观看结果。它不改变上文在板 R14，也不表示已复现 TomMao PSP 多任务模型。

| 功能 | 实现范围与边界 |
| --- | --- |
| 摄像头与 HDMI | 640×480 RGB565 视频；彩色、Sobel 边缘、灰度、去雾模式；视频继续走 HDMI，网页同步数据与控制 |
| 运动目标框选 | 固定摄像头前景/运动分割，最多 8 个目标；R13 改进真实时间速度估计、关联与多尺度续跟；不是语义车辆分类器，不保证识别初始化时已静止的障碍物 |
| 交通灯与倒计时 | 红/黄/绿灯及邻近 1–2 位七段数字（0–99）的受限场景识别；泛化准确率仍需目标场景验收 |
| 前向测距 | 单个 ToF 测距，危险 ≤300 mm、预警 ≤800 mm；失效/过期显示 NO DATA；不能给每个图像框分别测距 |
| 扩展屏与传感器 | 全英文页面、真实软时钟、行程计时、环境与姿态；默认六轴相对 Yaw，未验证绝对磁北 |
| 原生 500 万像素拍照 | 2592×1944 JPEG 静态照片；配置 4000 ms 稳定等待，拍照阶段同步暂停视频/UI/分析后恢复；不是 500 万像素连续视频 |

R13 离线视频的 7 张稀疏标注帧共选取 23 个目标框：10 fps 采样下，IoU≥0.5 的匹配数由 R12 的 11 个增加到 R13 的 15 个，平均最佳 IoU 由 0.4721 提升至 0.5478。该小样本结果不等于整体准确率；未建立完整误检、召回率或标准 MOT 评测。宿主运行时间也不代表 ARM 耗时或端到端视频延迟。

## 2026-10-30 测距恢复复测

本轮排查确认ToF驱动已烧录，历史R3与正式R13在未断电时均曾出现无效测量。用户彻底断开5V和USB后测距恢复，并已恢复正式R13综合版；重新放好纸板后，网页40/40快照有效（317–340mm、WARNING），最终20/20有效（361–383mm）。纸板位置手动变化，数据不是精度标定或独立帧准确率。详见[测距优先排查记录](board_validation/tof_priority_20261030/validation.md)。本轮未发布源码中尚未完整集成的新跟踪/恢复补丁。
## 历史整机板测：2026-10-29，R3

[整机验证记录](board_validation/run_all_20261029_192013/validation.md)使用 `CAMERA_SYNC_R10LCD_R3_20261028`，确认双屏正常，彩色/Sobel/灰度/去雾帧率分别为 16.469/16.534/16.415/16.523 fps，网页模式与识别开关同步，原生 JPEG 完整解码，拍照后视频和 IMU 恢复。ToF 初始化完成且有有效距离；时钟、IMU、环境数据在线。这些数值属于 R3，不能作为 R13 帧率或延迟结论。

同日[早前测距记录](board_validation/run_20261029/validation_20261029.md)：近处纸板 30/30 有效（230–252 mm），中距 27/30 有效（413–512 mm），其余读数相位失败并保留为无效。完整三级距离阈值、交通识别泛化、图像处理视觉质量和实体按键/触摸仍需现场逐项核对。上述下载为 JTAG RAM 启动，不代表断电固化。

## 历史 ToF R3 离线开发记录

- 默认版恢复原有的无软复位初始化顺序；每个 ST 初始化调用同时检查 API 返回值与 I2C 错误，并记录失败步骤。
- EnableTofSoftReset 只生成独立实验版，不替换默认 ELF。复位阶段具备时限、读取次数上限及失败释放处理。
- 本轮生产驱动宿主测试：默认 35 项、实验 53 项，共 88 项通过；两种 ARM ELF 均严格编译、链接与内存布局检查通过。
- 两份 ELF 的诊断地址均已重新生成，分别匹配各自 SHA256。生成过程只读本地 ELF，没有连接板卡。
- 保持原校准、33 ms 测量预算和光学质量阈值。无效 RangeStatus 与 8190/8191 不作为有效距离；未伪造数值。
- 此记录生成于 2026-10-28；当时尚未恢复有效测距。后续 2026-10-29 已取得上文有效距离证据，偶发无效读数仍按 NO DATA 处理。

详见 [现场验证记录](board_validation/现场验证记录.md)、[ToF 测试说明](verification/tof_reset_tests/README.md) 和 [R3 离线报告](board_validation/tof_r3_offline_report.json)。下载应使用完整系统复位、配对 FPGA 硬件、PS7 初始化和对应 ELF 的完整流程，避免跨内存布局的 CPU-only 下载。未经用户明确授权，不连接板卡或执行下载。

## 目录

| 路径 | 内容 |
| --- | --- |
| [firmware/src](firmware/src/) | ARM 裸机应用源码、全英文 UI、软时钟与串口命令 |
| [firmware/release](firmware/release/) | 当前正式 `vehicle_assist_lcd_english.elf` 与链接映射 `.map` |
| [preview](preview/) | UI 离线预览；展示源码排版，不是实机拍摄 |
| [dashboard](dashboard/) | 网页数据/控制桥接与 UTC+8 自动对时 |
| [verification](verification/) | 构建脚本、字体生成与宿主验证工具 |
| [使用说明](docs/全英文扩展屏使用说明.md) | 时钟、计时、各菜单页面与操作说明 |
| [时钟测试说明](verification/clock_tests/README.md) | 对时协议、无同步状态、续走与离线测试范围 |

## 构建完整应用（离线）

在本目录打开 PowerShell，执行：

```powershell
.\verification\build_software.ps1 -ToolRoot 'D:\FPGA\2026.1' -CameraControl 14 -EnableObstacles -EnableWebSync -EnableManualDetection -EnablePhotoSettle -PhotoSettleMs 4000
```

输出为 `firmware\release\vehicle_assist_lcd_english.elf`。本命令只编译应用，不会下载到板卡。上述完整功能开关需保留；ToF 与遥测默认启用，实验性 ToF 软复位默认关闭。脚本使用本机的 ARM GCC 和原有 `vehicle_system` BSP；若本目录提供 `bsp\standalone_domain`，则优先使用它。本应用继承现有 PL 硬件设计，当前目录的发布物是 ARM 应用 ELF；BIT/XSA/PS7 初始化脚本必须来自同一轮硬件发布。Hub 地址固定为 `0x40000000`。

## 启动网页与时钟同步

启动前先停止占用同一串口的旧桥接程序，只保留一个串口读取程序。在本目录运行：

```powershell
python .\dashboard\server.py --http-port 8767
```

然后打开 [车辆页面](http://127.0.0.1:8767/#vehicle) 或 [摄像头页面](http://127.0.0.1:8767/#camera)，选择实际 COM 口连接。若本机 `python` 未加入 PATH，可改用已安装的 Python 可执行文件完整路径。`8767` 若已被旧服务占用，应先停止旧服务或选择其他空闲端口，并相应修改访问地址。

收到板卡有效实时遥测后，桥接程序按电脑时间转换为 Asia/Shanghai（UTC+8）发送对时命令，每 60 秒校正，拍照期间暂缓。旧网页桥不提供此时钟同步能力。回放模式不会向串口发送时间或控制命令。

板卡初次启动时中央显示 `--:--:-- / WAITING FOR PC SYNC`。同步后断开串口仍继续走时；断电或 CPU 重启后需再次同步。下方 `TRIP TIMER` 与真实时钟独立。

## 此前 UI 与时钟离线检查记录（非本轮重测）

- 时钟及真实 UART 命令解析器宿主测试通过：未同步状态、跨日/月/年、闰年、非法日期、断线续走、命令长度和 PHOTO/CAL/OBSTACLE 共存。
- Python 时钟、网页桥接、硬件同步协议、照片和遥测等 54 项回归通过，使用假串口，没有访问真实板卡。
- 完整 ARM 严格编译通过，33 个实际 C 绘图页面/状态无文字越界。`preview/english-ui-overview.png` 为模拟数据排版预览，`preview/home-clock-board.png` 为板上 framebuffer 读取结果。
