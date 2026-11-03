# 设计源码

- `rtl/`：本提交含三个接口/叠加 RTL 文件，不包含完整 Vivado 顶层、约束与 IP 工程。
- `firmware/src/` 与 `firmware/network/`：ARM 应用与网络协议。
- `dashboard/`：标准库网页串口桥；从仓库根目录运行 `python src/dashboard/server.py --http-port 8767`。
- `pc_vision/`：GPU 协同检测、跟踪及网口工具，依赖与权重准备见该目录 README。

本轮只调整目录与引用路径，未修改固件算法。
