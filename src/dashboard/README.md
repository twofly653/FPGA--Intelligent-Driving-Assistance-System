# 当前 R9：两端统一相对方向

按用户最新要求，网页指南针与 LCD 统一使用六轴 ESKF 相对方向。LCD 与 TEL 共用整数取整，网页直接显示 TEL 整数。界面标注 REL，相对启动方向，无需校准，也不宣称磁北。真实数据失效时清除读数。

打开 http://127.0.0.1:8765/#vehicle ，首次更新请 Ctrl+F5。以下 CMP / 磁校准协议为保留的诊断功能，R9 相对模式不使用它来驱动指南针。

# PC 姿态与传感器遥测

正式浏览器入口恢复为此前独立运行的 **DriveSense Studio 原界面**：高精车模、
Dynamics / Camera / Sensors 三页及素材许可均完整保留在 `drivesense/`。
固件通过 115200、8N1 串口输出 TEL 姿态与独立 CMP 磁指南针。
电脑接收显示；网页校准按钮只发送限定的 CAL 命令，不向板卡下载程序。

## 推荐：浏览器实时页面（Windows 无需安装依赖）

在 PowerShell 执行：

```powershell
cd D:\project\FPGA_PYNQ_Integrated\dashboard
python -B server.py COM10
```

浏览器打开 http://127.0.0.1:8765/ 。COM10 换成实际端口；也可只运行
`python -B server.py`，在原有网页顶栏选择端口并点击“连接”。“刷新”只枚举端口，
不会打开串口；“断开”释放端口。若显示端口被占用，先关闭 Vitis Serial Terminal、
PuTTY、SSCOM 或桌面版，然后重新连接。

也可双击 `drivesense\start_demo.cmd`：它会启动同一桥接服务并打开原界面，
再在连接栏选 COM 口。页面中的“仅演示”会先释放真实串口，再显式切换 SIMULATION；
真实串口/回放模式下滑块、快捷场景、Reset、鼠标姿态拖动和自动模拟均锁定。
Camera 页在未接摄像头时始终保留 SIMULATED SCENE，不会因 IMU 连接而变成实测视频。

桥接使用 Windows 原生串口与 Python 标准库 HTTP/SSE，不需要 pyserial/websockets。
原界面的 Three.js r146、GLTFLoader、车模及纹理均在本地，CC BY 4.0 与 MIT 许可
见 `drivesense/third_party.md` 与 `drivesense/vendor/three_license.txt`。
网页启动与显示无需外网；非 Windows 平台只需额外安装 pyserial。

固件以 25 Hz 输出 TEL；桥接收到完整帧立即推送，不再等 read(256) 的 200 ms 超时，
也不再以 WebSocket 定时轮询。浏览器使用显示器刷新率绘制、18 ms 时间常数的姿态
平滑，roll/yaw 按最短角度变化，避免 359°→0° 绕圈；不外推或模拟真实姿态。页头显示实际接收帧率
与“接收后”数据年龄，该数字不等于传感器到屏幕的总延迟。

未收到有效数据、姿态未初始化时显示破折号；数据年龄超过 0.5 秒判定 STALE，
模型变暗并明确提示失效。串口错误和本机服务断连也立即标记。温湿度、气压、光照
与距离卡片沿用固件的有效标志；相对 Yaw 明确标注 REL，不能当作磁北。

### 磁指南针：与板端同源

车身三轴旋转继续使用 TEL 中的六轴相对 Yaw。右侧 Compass 单独使用板端
`compass_service` 的 CMP 磁航向；两者不互相替代。指南针读数使用与 LCD 一致的
`floor((heading_x10 + 5) / 10) % 360` 显示整度，例如 132.7° 显示 133°、359.9° 显示 000°。
接收延迟会导致转动中的短时帧差，静止时应显示同一读数。

网页展开“磁指南针校准与对齐”后：

1. 远离磁铁、金属、扬声器和大电流线。板卡平放，点“开始采集”，缓慢转满两圈；完整覆盖且拟合合格后自动结束采集并锁存结果。
2. 观察板端自动进入“待设置参考方向”状态。未完成时可点“结束采集”检查质量；不合格会继续采集。
3. 固定一个板卡前向边，让它与手机选定的磁北方向平行。手机与磁力计保持距离，
   避免手机自身磁铁干扰。填入当前手机磁北角，保持板卡方向，点“设置参考方向”。
   输入框初值 0°；请填写当前手机磁北读数，初值不是已完成的方向标定。
4. 顺时针转约 90°，检查读数也增加约 90°。如相反，点“翻转转向”，然后重新设置参考。
   至少核对三个不同方向后，才点“已核对多方向”。

设置参考只是安装方向对准，单个角度匹配不能证明磁校准成功或各方向精度。
未核对方向时仍会显示候选磁角并标 `VERIFY DIRECTION`；未校准、未设置参考、倾斜或
磁数据超过 0.5 秒时显示失效状态，不拿相对 Yaw 冒充磁北。
当前是平放二维指南针，不作三轴倾斜补偿；板卡重启后需重新校准。
手机若显示真北而板端显示磁北，两者还会有当地磁偏角差，应先统一比较基准。

### 轴向

固件 body 为 **+X 前、+Y 左、+Z 上**；TEL heading 顺时针，roll 正使左侧抬起，
固件 pitch 正使车头下俯。原 DriveDemo API 的 pitch 约定为“正值抬头”，
桥接仅在 `drivesense/attitude-adapter.js` 的 `toFrame` 中执行一次 `pitch=-TEL.pitch`，
所以电脑该页 pitch 读数与板端采用不同的正方向，实际车身动作一致。

高精车模 +Z 前/+X 左/+Y 上，使用 `Ry(42-heading) × Rx(-API.pitch) × Rz(roll)`；
42° 只保留原展示视角，不叠加到航向读数。备用几何车模前向 -Z，使用等价轴映射。
3D 车身相对 Yaw 与独立磁指南针互不覆盖，指南针保留 N/E/S/W 方位刻度。
桌面可选界面保留固件角度正方向，采用其 +X 车头场景的等价旋转。
板上仍须依据 IMU 安装方向实测核对 `attitude_board_config.h`，本软件映射不能代替安装校准。

### 离线回放与接口

```powershell
python -B server.py --replay ..\release\validation\dashboard_replay.tel --http-port 8879 --loop
python -B -m unittest -v test_telemetry_protocol test_dashboard_bridge
node test_attitude_math.mjs
node test_drivesense_adapter.mjs
```

回放会显著显示“回放验证 · 非板卡实测”，不会打开任何 COM 口。未加 `--loop` 时
回放结束后自动进入 STALE。接口均限本机回环地址：

- `GET /api/status`：当前数据、有效性、来源、接收帧率及年龄。
- `GET /api/ports`：只枚举串口。
- `GET /events`：SSE 新帧实时推送；空闲时也发送状态，及时让旧值失效。
- `POST /api/connect`，JSON `{"port":"COM10"}`：连接真实串口。
- `POST /api/disconnect`，JSON `{}`：释放串口。
- `POST /api/compass`：只接受 `{"action":"begin"}`、`end`、`cancel`、`flip`、`confirm`，
  或 `{"action":"ref","heading_x10":740}`。参考必须为 0..3599 整数（0.0..359.9°）。
  仅真实 LIVE 串口允许发送，回放/未连接时拒绝；命令写入与串口句柄关闭共锁，
  不接受任意串口文本、路径或换行注入。HTTP 同源检查同时应用于校准操作。

原页面九项公共接口检查：`/tests/browser_api_smoke.html`；新增实时接入隔离检查：
`/tests/live_api_smoke.html`。两者使用显式 smoke 模式的 iframe，无串口连接。
`/legacy/` 仅保存另一套辅助页面作回归对照，默认入口始终为原 DriveSense。

## 桌面版（保留原有界面，可选）

在本目录运行：

```powershell
python -m pip install PySide6
python dash_app.py
```

选择当前板卡 COM 口，点击 Connect。串口只能由一个终端占用，使用本程序前请关闭 Vitis Serial Terminal 或其他占用同一 COM 口的程序。

桌面版同样使用低延迟原生串口，16 ms 绘制定时器、0.5 秒失效阈值及统一轴向。
滑块用于显式 DEMO（非板卡实测），真实串口连接期间禁用；断连后重新启用。
当前 Python 环境未预装 PySide6，优先用上面的免依赖浏览器方案即可。

## 数据格式与边界

```text
TEL roll_x10 pitch_x10 heading rr_x10 pr_x10 yr_x10 temp_x10 rh_x10 hpa lux range_mm level imu mag
CMP heading_x10 valid detected fresh calibrated aligned collecting level span_x span_y coverage samples raw_x raw_y raw_z reference_x10 direction direction_verified fit_error_ppm tilt_deg10 selftest_result selftest_status
```

角度/角速度的 x10 表示整数十分之一度/十分之一度每秒；heading 为顺时针角度。imu=1 才表示近期有有效姿态；mag=1 才表示可信磁航向，默认未提供三轴标定时为 0。环境数据过期以负哨兵值表示，前方无有效距离为 -1。

板端实际使用 ESKF；当前默认六轴姿态，磁力计的平放二维指南针校准与三轴姿态融合校准分开。PC 可视化不提高传感器精度。

CMP 单独以 10 Hz 发送；`span_x/span_y/raw_x/raw_y/raw_z` 的单位是 mG，覆盖为 0..8 个扇区，
`samples` 为有效校准样本数，`direction` 为 ±1，`fit_error_ppm` 为拟合残差，未知为 -1，
`tilt_deg10` 为十分之一度倾斜角，未知为 -1。旧 17 / 20 字段 CMP 可读取；17 字段不假设方向已核验。
R8 的 22 字段额外给出磁力计启动自检：`selftest_result` 为 0 未运行、1 PASS、2 SAT_FAIL、
3 INCONCLUSIVE、4 IO_ERROR、5 TIMEOUT；`selftest_status` 是 0..255 状态寄存器字节。
旧帧两项默认 0。自检 2..5 明确提示失败原因并隐藏磁角，优先于未检测到/未产生新样本提示。
自检 PASS 只表示启动自检通过，不能证明磁北精度，仍须磁校准与多方向比对。
`GET /api/status` 与 SSE 的 `compass` 对象保留原整数值，单独提供年龄/过期/有效状态。
TEL 的接收率只统计姿态帧，CMP 更新不影响该数字，也不会让过期姿态重新有效。

离线验证：16 项协议/桥接测试通过；覆盖半帧/连续帧、初始无数据、独立 TEL/CMP 过期、SSE、
回放标签、校准命令白名单/并发串行化及跨站控制拒绝；JS 验证原高精/备用模型各 200 组组合旋转、
单处 pitch 转换、相对 heading、磁指南针状态、回放与无效距离，并检查脚本语法。
`/tests/live_api_smoke.html` 额外检查指南针与姿态独立、LCD 同式舍入、方向待核验及倾斜失效。
测试均不打开实体串口。最终板端帧率、安装轴向与操作响应仍以真实板卡联测记录为准。
