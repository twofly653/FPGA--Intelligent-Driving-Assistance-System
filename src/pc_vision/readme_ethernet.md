# 以太网协同

板卡默认 `192.168.2.99`，电脑 `192.168.2.1/24`；UDP 端口为 50010 / 50011，固件配置见 `../firmware/src/pc_net_config.h`。

- `net_protocol.py`：协议、校验与重组。
- `ethernet_tracking.py`：接收板卡分析帧、检测与结果回传。
- `live_tracking.py`、`precision_tracking.py`：跨帧编号与关联。
- `quality_input.py`、`network_preview.py`、`latest_artifacts.py`：显示与有界预览输出。
- `auto_time_sync.py`、`start_live.py`：时钟同步和启动编排。

分析帧为 320×240 RGB565，主 HDMI 为 640×480。带会话、帧号、时间与 CRC 检查；仅处理最新帧。停用识别或断线时清除过期结果。源码不包含配套 BIT/XSA/ELF，需另行准备同一发布版本。

在电脑设置好实际网口地址并连接后，运行 `run_ethernet.ps1`。`-Tracker baseline` 选择基础关联，默认 `precision`；`-Headless` 关闭预览窗，`-PreviewDetail` 开启显示细节增强。这些参数不改变布线，不执行烧录。
