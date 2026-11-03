# 原生 OV5640 5MP 照片

此目录为 v4 独立网页副本，原 dashboard 与当前运行服务没有在本子任务中切换。原姿态、指南针、传感器、回放和模拟道路场景保留。本版预览硬件设计为 640×480 完整 RGB565，HDMI 640×480 1:1 输出；原生静态照片仍为独立的 2592×1944 JPEG。

## 使用

用具备 Pillow 的 Python 运行本目录 `server.py COM10`，在原 Camera 页点击真实照片卡的拍摄按钮。正式服务由主集成任务在硬件验收后启动。已有的本机 Codex Python 运行时具备 Pillow；照片功能不请求浏览器摄像头权限。

HDMI 实时预览仍在主板 HDMI OUT 外接显示器。网页概念道路画面仍是模拟。照片仅在 CRC32 和原生 `2592×1944` JPEG 完整解码校验通过后显示；原图下载保持字节不变，不做放大或重编码。

单颗传感器拍照时切换到 5MP JPEG，再恢复预览，因此 HDMI、LCD 和姿态会短暂停顿。取回 DDR 原图在后台进行，串口继续接收 TEL；照片不通过 115200 串口传输。

## API

- `POST /api/photo`，JSON 空对象 `{}`：仅 LIVE 真实串口已连接时创建一个互斥任务，返回 202；模拟/回放/未连接或已有任务返回 400。继承原同源 Origin 检查。
- `GET /api/photo`：读取任务状态，不发送拍照命令。阶段为 idle / arming / capturing / transferring / verifying / ready / error；使用阶段说明，不虚构进度百分比。
- 成功 `photo` 字段包含 `id`、`board_id`、`width=2592`、`height=1944`、`size`、`crc32`、`verified=true`、`verified_at_ms`、`url`、`filename`。时间是电脑完成校验时间，不是传感器曝光时间。
- `GET /photos/<id>.jpg`：仅允许本次服务会话中自身已校验发布的照片；不允许访问 `.work`、任意路径或未经验证文件。照片保存在本目录 `photos`。

## 数据路径

Bridge 是唯一串口接收者。任务先发送 PHOTO STATUS 确定板卡状态，再只发送一次 PHOTO SNAP；必须在本次请求后观察 BUSY→相同编号 READY。这样既能拒绝旧 READY，又支持 CPU 重启后编号重复。

元数据只允许 2 MiB 以内的原生 5MP 图像，完整按字对齐的读区间必须位于 DDR `0x00100000..0x1FFFFFFF`。工作线程运行固定本机 `D:\FPGA\2026.1\Vivado\bin\xsdb.bat`，APU/DAP 仅执行 `mrd -bin`；不停止 CPU、不下载、不复位。若 3122 缺失，可按需启动同一端口的 hardware server，不修改其他服务端口。进程隐藏窗口，命令参数来自服务器自身路径及已验证整数。

收到原始二进制后先按真实长度截尾，再比较板端 CRC32，最后用 Pillow verify/load 完整解码。任一错误均不发布照片。任务 Tcl、日志和原始读取数据位于 `photos/.work/<job_id>`，均在 D 盘。

## 离线验证

```powershell
python -m unittest test_photo_capture test_dashboard_bridge test_telemetry_protocol
node --test test_attitude_math.mjs test_drivesense_adapter.mjs
```

已通过 22 项 Python 回归和 2 个既有 Node 测试文件。新照片测试覆盖地址/长度/注入拒绝、CRC/真实 JPEG 解码/错误尺寸、请求只发送一次、旧 READY 拒绝、重启编号重复、任务互斥、状态及采集超时、失败不发布、DAP 只读脚本和 HTTP 同源/照片 allowlist。测试不连接真实串口、XSDB 或板卡；HTTP 测试仅使用临时本机端口。
