# 构建入口

在仓库根目录运行 `./build/build_software.ps1`；完整参数见根 README。`build_pc_network.ps1` 调用同目录基础脚本。

编译需要本机 AMD ARM 工具链及匹配 OB02 的 standalone BSP。当前包不含 BSP、BIT、XSA、ELF 或完整 Vivado 工程，不能在缺少依赖时从本仓库独立重建全部硬件。脚本只编译应用，不下载板卡。

构建输出仍按脚本写入 `src/firmware/build/` 和 `src/firmware/release/`，这些为忽略的生成文件。仿真与板测中的 JSON 为已有记录，不是本轮新综合报告。本包没有可供提交的完整 FPGA 综合/时序报告；获取完整工程后应补齐。
