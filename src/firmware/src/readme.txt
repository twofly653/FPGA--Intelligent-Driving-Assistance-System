# 摄像头 V4：RGB565 DDR VGA 预览与原生 5MP JPEG（2026-10-23）

V4 启动时 profile 候选14并设置 DDR_READY bit15；拍照和预览恢复保持该位。
应用/照片缓存/堆栈限制在0x10000000以下，PL预览三帧位于0x10000000/10100000/10200000。
最终 profile 待离线构建后的板测决定；当前未执行下载。

当前新增 PHOTO SNAP / PHOTO STATUS，原生 2592×1944 JPEG 写入2MiB DDR缓存，
电脑端 DAP 取回后校验 CRC32 并完整解码。详见 firmware/拍照接口与验证.md。
拍照期间切换单颗传感器模式，预览与UI/IMU短暂停顿，随后恢复并重新对齐姿态。
计时按真实经过时间补齐；其他旧输入分工保留。

# 摄像头 V2 与扩展板历史入口（2026-10-23）

本目录仅供 camera_system_v2 使用，稳定 R9 源码保持原样。
使用新工程 verification/build_software.ps1 生成 firmware/release/vehicle_assist.elf。
启动后约两秒自动进入首页，BTN0 仍可重新启动应用。
首页五向键：左复位、右开始、上暂停、下仅从暂停恢复；中键打开原菜单。
计时继续在后台运行，其他页面的五向键只做原导航，不改变计时状态。
BTN1/BTN2/BTN3 专用于 PL 摄像头的车道线/灰度/去雾模式；再次按当前键回彩色。
保留原传感器、六轴姿态、同源相对方向及 TEL 遥测格式。
启动末尾 [CAMERA] 输出只读 MMIO 快照；不是实时帧率统计，也不写摄像控制寄存器。

以下为旧工程历史说明，旧按键表与当前版本不一致。

# 整合版历史入口（2026-10-17）

此目录是整合版PS源码。以工程根目录 README.md、release/README_手动下载.md 为当前说明。
使用 scripts/build_software_2026.ps1 生成 release/software/vehicle_assist.elf；
Vitis 2026.1 User Managed Mode 手动下载 release 中的配套文件。
下文为学长原始2022.2说明，保留作历史参考，旧Debug产物不属于本次发布。

# intelligence — contest PS app (ported teammate sensors)

## Sensors / pages

| Path | Content |
|------|---------|
| 首页 | 行驶计时 LCD；BTN1/2/3；菜单（设置页已删） |
| 车载环境监测 | T / RH / P / Lux(lx) |
| 车载姿态航向 → COMPASS | 队友校准罗盘（平转一圈 + ALIGN） |
| 车载姿态航向 → IMU | Acc / Gyro |
| DISTANCE | VL53L0X 三级防撞（需 APP_ENABLE_TOF=1） |

## Build (Vitis 2022.2)

1. Refresh `intelligence` so new `.c` appear
2. Linker must include `-lm` (already set for compass)
3. Build Project → Run
4. UART: `ACK ... tof=...` and `VL53L0X ranging init=`

## PL base

`PL_HUB_BASE` fallback `0x40000000`. Match Vivado Address Editor if different.

## Do not open test1 Vivado 2026 `.xpr` in 2022.2

See `FPGA_project/PORT_FROM_TEST1.txt`.
