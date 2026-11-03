# DriveSense HDMI 姿态展示 Demo

## 2026-10-17 实时接入副本

当前 D 盘副本完整保留原三页与高精车模，新增 UART 实时桥接。
双击 `start_demo.cmd` 会运行上层 `src/dashboard/server.py` 并打开原界面；
在连接栏选 COM 口、点击连接即可查看固件 TEL。无需安装第三方 Python 包。
完整操作、数据有效性及轴向约定见 `../README.md`。

真实串口与回放模式禁止演示控件覆盖姿态；“仅演示”先释放串口。
来源区分 LIVE UART / REPLAY / SIMULATION，Camera 在无真实视频时继续显示模拟来源。
原 API `pitch` 仍为正值抬头；固件 pitch 正下俯，桥接入口仅变号一次。
板卡 HDMI OUT 输出仍未包含在本浏览器工程内。

以下保留原独立演示说明，提到“尚未接入”的段落描述的是原恢复快照。

这是一个**独立、离线可运行的三页面演示**。它用于先确认大屏视觉效果和交互方式，目前数据均为模拟值，尚未接入 PYNQ-Z2、IMU、摄像头或板载 HDMI 输出。v0.2 将原来的几何小车升级为有完整车身、玻璃、内饰、灯组和轮胎的 3D 概念车；v0.3 加入页面导航、摄像头感知框架和独立传感器页。

## 立即查看

Windows 上推荐双击 `start_demo.cmd`（需要 Python 3），它会在本机启动页面并自动打开浏览器；也可以直接双击 `index.html` 尝试离线打开。建议使用 Edge 或 Chrome。将浏览器窗口拖到通过 HDMI 连接的外接显示器，按 F11 或点页面右上角的全屏按钮。页面按 1920×1080 大屏设计，也会适配较窄窗口。无需安装 npm 依赖，也无需联网。精细车模需要浏览器支持 WebGL；若不可用，页面会保留原来的简化模型作为备用。

## 页面导航

桌面宽屏使用左侧导航，较窄的窗口使用底部导航。Dynamics 保留 3D 小车、姿态角和罗盘；Camera 提供道路场景、交通灯状态、近距告警、系统连接状态和 PS/PL 数据链路的交互式框架；Sensors 单独展示环境数据与传感器清单。可通过 `#vehicle`、`#camera`、`#sensors` 直接打开对应页面。切换页面会保留当前演示状态。

Camera 页的道路图是**矢量示意画面**，不是摄像头采集图；交通灯和距离按钮仅改变示例数据。未连接真实视频时页面会显示 `SIMULATED SCENE` 和 `NO CAMERA CONNECTED`，帧率与延迟保留为 `—`。

需要复核网页的数据入口时，先启动本地服务，再打开 `http://127.0.0.1:<端口>/tests/browser_api_smoke.html`。页面会用一张内嵌合成图片调用 `pushVisionFrame` 和 `pushFrame`，检查外部帧标签、红灯、距离、检测框、帧率以及切回模拟源后的状态；`PASS 9/9` 表示浏览器接口检查通过，不代表板卡或真实摄像头已接入。

## 操作

- PLAY / PAUSE DEMO：自动模拟小车转弯、俯仰和航向变化。
- ROLL / PITCH / YAW：拖动三个滑块，观察小车、数值和罗盘同步变化。
- 鼠标拖动小车区域：调整航向与俯仰；滚轮调整模型大小。
- LEVEL ROAD / SHARP TURN / STEEP SLOPE / CLOSE OBJECT：切换典型场景，最后一项会演示三级距离状态中的危险级。
- RESET POSE：回到水平姿态。
- 在 Camera 页点 Red / Amber / Green / Unknown，或拖动距离滑块，预览识别与告警的界面状态。这些操作不代表真实识别或测距。

顶部标有 SIMULATION MODE，指南针与环境区域也标有 DEMO。演示数据不代表真实传感器读数。

## 后续接入入口

页面的 app.js 暴露了全局接口 window.DriveDemo.pushFrame(frame)。以后 PS 程序可通过本地 WebSocket 或网页接口，将**已经融合好的姿态角**送入页面，例如：

~~~js
window.DriveDemo.pushFrame({
  roll: -12.5,        // 度，侧倾
  pitch: 4.0,         // 度，车头上仰为正
  heading: 85,        // 度，0 = 磁北，顺时针增加
  temperature: 26.4,  // °C
  humidity: 48,       // %
  pressure: 1013,     // hPa
  lux: 460,           // 估算 lx
  distance: 2.8       // m
});
~~~

setPose({roll, pitch, heading}) 可只更新小车姿态。原始陀螺仪角速度不能直接当作姿态角，需要积分并结合加速度计校正；航向还需要磁力计校准和板卡安装方向确认。

Camera 页有独立入口 `window.DriveDemo.pushVisionFrame(frame)`，向 `window.DriveCameraView.pushFrame(frame)` 转发感知数据。`frame` 可包含 `trafficLight`（RED / AMBER / GREEN / UNKNOWN）、`distanceM`（米；`null` 表示无有效读数）、`fps`、`latencyMs`。Camera 与 Sensors 页的三级测距示意阈值统一为危险 `≤ 0.30 m`、预警 `≤ 0.80 m`、其余安全；Dynamics 的 CLOSE OBJECT 预设距离为 `0.25 m`。只有在调用方确实提供可访问的图像 URL 时，才使用 `source: "live"` 与 `imageUrl` 显示外部帧；可选 `detections` 数组存放 `{x,y,width,height,label}`，位置和尺寸按画面宽高归一化到 `0–1`。页面自身没有摄像头采集、视频传输或交通灯识别能力。未来板卡与浏览器之间还需要实现实际的视频和数据传输。

## 与板卡 HDMI 的关系

当前演示页运行在**电脑浏览器**中：电脑 HDMI 接外屏即可展示。若要求由 **PYNQ-Z2 的 HDMI OUT** 独立输出，需要另行实现板上的视频输出与页面渲染/图形显示链路；本 Demo 未包含该部分。此阶段可以先用它确认视觉设计和传感器数据协议，再选择板上实现方式。

文件：index.html、styles.css、app.js、navigation.js、camera-view.js、camera-view.css、car3d-source.js、car_model_data.js、assets/、vendor/、start_demo.cmd、launch_demo.py。全部使用相对路径，请保持文件夹结构，复制到任意英文路径后打开。车模和渲染库的来源与许可见 third_party.md。精细模型是外部授权素材的修改版，不应在比赛材料中声称由团队从零建模。
