# R13 TRACK 综合版烧录与启动验证（2026-10-30）

用户明确授权“我已经插上板卡，烧录”后，在原工程通过 JTAG 下载。没有创建工程副本、修改系统设置或更改摄像头接线。

## 下载文件

- 正式软件：`src/firmware/release/vehicle_assist_lcd_english.elf`，1,147,852 字节，SHA256 `f308da03d9d0feb7bed1c9057f4ef974b6378c597a6e930d929b3cbef594d36e`。
- BIT、XSA、PS7 来自同一硬件发布目录 `D:/project/deliveries/Task3_Submission_20261026/github/PYNQ-Z2-Vehicle-Vision/current_release`，三个 SHA256 与该目录 manifest 相符；本轮未改 RTL。
- 使用 `../download_lcd_full.tcl`，完成系统复位、FPGA 下载、PS7 初始化、OB01 magic 检查及正式 ELF 下载运行。
- 下载日志（历史生成文件或外部参考未随包提供：`download.log`） 返回 `LCD_ENGLISH_CLOCK_FULL_DOWNLOAD_COMPLETE`；启动串口（历史生成文件或外部参考未随包提供：`startup_uart.log`） 确认 `CAMERA_SYNC_R13TRACK_20261030`。

## 本轮真实板测

用户确认：**HDMI 有实时图像，扩展屏正常**。

| 检查 | 本轮结果 | 证据 |
| --- | --- | --- |
| 摄像头 | OV5640 ID、配置正常，原生 640×480 RGB565 持续出帧 | `startup_uart.log` |
| 彩色 / Sobel / 灰度 / 去雾 | 分别 16.567 / 16.578 / 16.447 / 16.437 fps；DDR 故障位均为 0；已恢复原彩色模式 | `modes.log` |
| 扩展板 | LCD home、CST816 触摸、环境传感器完成启动；用户确认小屏正常 | `startup_uart.log` |
| ToF | 初始化 ready=1、stage=16、API=0、I/O=0；运行中网页出现 NO DATA 和 WARNING | `runtime_after_web.log`、`web_samples.json` |
| 时钟 | 网页连接后 CLOCK_VALID=1，首页时钟已对时 | `runtime_after_web.log` |
| 识别手动开关 | OFF、ON 均收到新的 DETECT 板端确认；OFF 清除网页目标框；最终保持 ON | `web_checks.json` |
| 连续网页通信 | 12 秒 56 次采样均连接、新鲜摄像头、live、识别开关新鲜、IMU 有效 | `web_checks.json` |
| 跟踪结果更新 | 50 个不同结果序号，48/56 次采样可用，其余包括重新开启后的初始化或过期；处理时间 11–44 ms | `web_checks.json`、`web_samples.json` |

本轮连接网页前先用只读串口捕获启动日志。首次网页会话曾出现视频元数据过期；原始 UART 仍在输出。重新连接 COM10 后，以上连续采样正常。没有据此修改固件；该现象的具体原因尚未证明。

## 验收边界

- 本轮证明正式 R13 已运行、视频帧率及板端/网页基本联动正常；尚未完成用户视频经屏幕播放、摄像头拍摄后的车辆跟随精度验收。当前场景的绿框不能用来证明车辆分类准确率。
- 固定摄像头运动区域跟踪，最多 8 个框；遮挡、车辆粘连及局部重复框仍可能发生。算法不提供车型语义、逐框距离或自动驾驶决策。
- 帧率不是端到端延迟测量；ToF 正常启动和 WARNING 状态不是测距精度验收。
- 未在本轮重新验收 5MP 拍照色彩、交通灯/倒计时准确性、去雾视觉效果、所有触摸页或磁航向。已有历史记录不能替代本轮专项验收。
- 网页运行于 [http://127.0.0.1:8767/#camera](http://127.0.0.1:8767/#camera)，视频仍由 HDMI 输出。

离线车辆视频对比与限制见 [R13 车辆跟踪验证](../../sim/vehicle_tracking_video_20261029/README.md)。完整文件哈希见 [本轮发布记录](release_manifest.json)。
