# 板上验证与历史下载工具

本目录保留已有板测、ToF、相机、字体和网络诊断证据。`r13_20261030/validation.md`、`r14_tracking_20261030/validation.md` 说明对应历史条件。

下载 Tcl 保留原机器发布路径，仅作版本追溯；换机必须先提供同一发布版本的 BIT、XSA、ELF、ps7_init.tcl 并调整路径。当前仓库缺少前三种二进制，不要直接运行历史下载脚本。本次整理没有连接、复位或烧录硬件。

当前提供的 PS 初始化文件保留在 `../src/firmware/golden_ob02_bit/` 和 `../src/firmware/release_ob02_20261101/`。它们不能替代完整硬件发布文件。
