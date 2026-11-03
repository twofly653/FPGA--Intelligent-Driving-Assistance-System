# R3 板卡下载与验证记录（2026-10-29）

本记录为实际连接板卡后的结果，与 2026-10-28 的离线测试分开保存。

- 用户授权：板卡插好后，执行“烧录验证”。
- 固件：CAMERA_SYNC_R10LCD_R3_20261028，默认版，不启用强制 ToF soft-reset。
- 连接：COM10，115200 8N1；网页 http://127.0.0.1:8767/。
- 下载：完整系统复位、FPGA 下载、匹配的 XSA/PS7 初始化、ARM ELF 下载与启动。XSDB 返回 0。
- ELF SHA256：f96acef27206d58148a902f277d709aabaad9700b890d421d06848d33daabee1。
- 配套硬件文件：保留的 current_release/vehicle_top.bit、vehicle_top.xsa、ps7_init.tcl；本次已验证 BIT/PS7 init 与 XSA 对应项一致。

## 实际结果

| 项目 | 结果及证据 |
| --- | --- |
| 扩展屏、HDMI | 用户确认两者正常显示。UART 启动出现 LCD home up。 |
| 摄像头 | 实时/configured 均为 1，640×480，彩色模式 0，控制值 0x0000800E。 |
| 传感器数据 | 网页实时连接，温湿度、气压、光照及 IMU 有数据。 |
| 自动校时 | 启动前 CLOCK_VALID=0，网页连接后为 1。采样时界面在菜单/测距页，未另行验收首页时钟跳秒。 |
| ToF 初始化 | ready=1、running=1、stage=16、result=0、API=0、I/O=0、reset_enabled=0。 |
| 近距离纸板 | 用户确认纸板正对 ToF、约 20–30 厘米；30 次被动观察均 RangeStatus=0，230–252 mm，I/O=0。 |
| 目标移开 | 随后网页与原始测量均无有效距离；用户确认纸板已移开。此组无目标数据不计为纸板测试失败。 |
| 中距离纸板及同步 | 用户按约 50–60 厘米摆放；30 次原始观察中 27 次有效，413–512 mm；3 次 phase fail/8190。同步网页 63 个快照中 58 个有有效距离，356–512 mm，状态 WARNING；其余 NO DATA。无 API 错误。用户摆放距离未用尺校准，以上不构成精度标定。 |
| 指南针 | 本轮网页 detected=0、valid=0；启动日志 MAG SELFTEST IO_ERROR。未取得有效磁航向，未宣称恢复。 |

原始观察每 250 ms 一次，测距服务周期为 200 ms；网页快照每约 200 ms 一次，可能重复同一条遥测，样本数不是独立测量帧数。

## 结论和未完成项

默认 R3 已下载并运行；近距离纸板条件下已取得连续有效测距，网页能同步有效距离和 WARNING。
中距离仍出现无效测量，尚不能宣称所有距离下测距稳定、精度合格或全部三级预警完成验收。
本轮没有下载独立 soft-reset 实验版，没有降低光学有效性标准，也没有把 8190/8191 当成距离。

本次未重新逐项测试 Sobel/车道线、灰度、去雾、交通灯/数字识别、运动目标开关、5MP 拍照和计时操作；保留功能不代表这些项目已在本轮复验。指南针需要后续排查。

## 证据文件

- download_default.log、startup_uart.log：本次下载与启动。
- runtime_initial.log、runtime_after_card.log：匹配当前 ELF 的有限运行状态读取。
- range_default_card.tcl、range_default_card.log：近距离纸板 30 次观察。
- web_card_20_30cm.json：实际为纸板移开后的快照，文件名沿用首次准备名；不得当作有目标条件结果。
- range_card_50_60cm.log、web_card_50_60cm.json、summary_card_50_60cm.json：中距离原始数据与网页同时采样。
- validation_summary.json：本次结构化摘要。

网页桥接保持运行，供继续操作和检查。
