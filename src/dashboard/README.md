# 网页与串口桥

从仓库根目录运行 `python src/dashboard/server.py --http-port 8767`，浏览器打开 `http://127.0.0.1:8767/` 并选择实际 COM 口。Windows 桥接服务使用 Python 标准库；其他平台需 pyserial。

`drivesense/` 是三维车模、摄像头状态和传感器主界面。原有 `/legacy` 页面仍由服务提供。`photo_capture.py` 处理拍照作业，`telemetry_protocol.py` 处理协议。必要的离线页面资源和第三方许可保留。

视频通过 HDMI 输出；网页同步状态和控制。串口只能由一个程序占用。素材来源见 [第三方说明](drivesense/third_party.md)。
