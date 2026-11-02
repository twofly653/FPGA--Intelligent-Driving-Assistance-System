param([string]$ToolRoot='D:\FPGA\2026.1')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'build_software.ps1') -ToolRoot $ToolRoot -OutputName vehicle_assist_pcnet_r15_candidate.elf -EnablePcNetwork -EnableObstacles -EnableWebSync -EnableManualDetection -EnablePhotoSettle -PhotoSettleMs 4000 -KeepBuildFiles
if($LASTEXITCODE -ne 0){throw 'PC Ethernet candidate build failed'}
# Compile/link only. This script contains no JTAG, serial, or download calls.
