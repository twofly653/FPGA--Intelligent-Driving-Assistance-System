# R13 fixed-camera tracking: video regression and reproduction

Current application banner: `CAMERA_SYNC_R13TRACK_20261030`. This directory records offline R12-to-R13 comparison on the supplied traffic video. Directory dates identify the experiment, not the final firmware build date. Read the [Chinese verification report](R13车辆跟踪优化验证报告.md) for method, limits and pending board checks.

The formal [ELF](../../firmware/release/vehicle_assist_lcd_english.elf) is 1,147,852 bytes, SHA256 `f308da03d9d0feb7bed1c9057f4ef974b6378c597a6e930d929b3cbef594d36e`; see [build log](build/formal_build.log). The separately recorded [2026-10-30 R13 board checks](../../board_validation/r13_20261030/validation.md) confirm this exact ELF was downloaded, both displays worked, four video modes sustained 16.437–16.578 fps, and live web control/data passed. These board checks do not establish vehicle-tracking accuracy or camera-to-HDMI latency.

## What changed

- Velocity uses capture timestamps and pixels/second rather than assuming a fixed frame interval; association and reacquisition use bounded prediction.
- Confirmed-track appearance continuation searches 70/85/100/115/130% scales. Display requires support from current foreground and appearance; prediction alone never creates a displayed box.
- Tentative templates learn from gated component matches; appearance-only continuation does not rewrite templates. Confirmation handles variable sampling intervals while retaining three hits, at least 80 ms and actual motion/size support.
- Source age over 500 ms and source gaps over 750 ms invalidate tracking history. Maximum eight targets; 320×240 RGB565 input, 160×120 internal analysis, boxes mapped to 640×480 by the service. R13 state is 450,372 bytes.

## Evidence

| Artifact | Purpose |
| --- | --- |
| [run_manifest.json](results/run_manifest.json) | Input hash, R12/R13 source hashes, compile commands and descriptive run statistics |
| [contracts manifest](build/contracts/manifest.json) | Five host regression groups, return codes, exact commands and source hashes; `hardware_access:false` |
| [sparse labels](sparse_labels.json) / [sparse scores](results/sparse_scores.json) | Seven manually selected frames, 23 selected boxes, greedy one-to-one IoU matching |
| [comparison video](results/R12_R13_video_comparison.mp4) / [preview](results/R12_R13_preview.png) | Actual 10 fps replay outputs for source seconds 5.5–15.0, without interpolating boxes |
| [comparison manifest](results/comparison_manifest.json) | Rendering method and disclaimer |
| [5 fps contact sheet](results/fps5_stable_contact.jpg) / [10 fps contact sheet](results/fps10_stable_contact.jpg) | Stable-interval inspection |
| [source overview](source_contact.jpg) / [source timeline](source_timeline.jpg) | Scene and playback/UI inspection |
| [historical R12/R11 record](../motion_precision_20261029/README.md) | Earlier board captures and replay repeatability; separate from R13 offline evidence |

| Sampling | Variant | Selected boxes | IoU≥0.3 matches | IoU≥0.5 matches | Mean best IoU | Sparse ID changes at IoU≥0.3 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 10 fps | R12 baseline | 23 | 16 | 11 | 0.4721 | 3 |
| 10 fps | R13 candidate | 23 | 18 | 15 | 0.5478 | 3 |
| 5 fps | R12 baseline | 23 | 16 | 13 | 0.4503 | 4 |
| 5 fps | R13 candidate | 23 | 18 | 15 | 0.5153 | 1 |

These are sparse, scoped comparisons, not overall accuracy or standard MOT ID switches. There are no exhaustive false-positive labels or dense ground truth, so precision/recall is not established. The run manifest's `ground_truth:false` refers to the absence of dense ground truth; sparse manual labels are supplied separately. Counts and unique IDs are descriptive statistics, not accuracy. Host wall time includes I/O/JSON and is not ARM time or end-to-end latency.

## Reproduce offline

Run from `D:\project\FPGA_PYNQ_Integrated\lcd_english_20261028`:

```powershell
& 'C:\Users\twofl\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' .\verification\vehicle_tracking_video_20261029\run_contracts.py
& 'C:\Users\twofl\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' .\verification\vehicle_tracking_video_20261029\video_regression.py
& 'C:\Users\twofl\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' .\verification\vehicle_tracking_video_20261029\score_sparse.py
& 'C:\Users\twofl\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' .\verification\vehicle_tracking_video_20261029\render_comparison.py
```

Dependencies: NumPy, Pillow, local GCC at `D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe`, FFmpeg at `D:\Software for daily live\EVCapture\ffmpeg.exe`, and the original video at `C:\Users\twofl\xwechat_files\wxid_e0zynr4vepxa22_8545\msg\video\2026-10\0d325868f3e3e5c37d0c592783d2852c.mp4`. The script currently uses these local paths; another machine must supply equivalent paths. Original video SHA256: `5a66062f1f4bd570cacad68bc8d9421b9f0dd9c93f8ab7cc395c2e7a69a46fdb`.

The source is 1280×584, 22 fps, 24.685 s. It is area-scaled to 320×146, padded by 47 black rows above and below, converted to RGB565LE and sampled at 5/10 fps with source-time timestamps. Full, stable (5.5–15.0 s) and late-UI (≥16 s) runs each cold-start. Pausing/controls at approximately 2–4.7 s and later UI/zoom changes make the stable interval the primary comparison. At 5 fps, nearest-frame sparse comparisons may differ by 100 ms.

`ablation.py` only inspects preserved historical ablation results; it does not reproduce the old source-string mutations against final R13. Optional `score_sparse.py --historical` writes a separate historical score file. The four commands above are the final reproducible comparison path.

To build the integrated formal application without downloading:

```powershell
.\verification\build_software.ps1 -ToolRoot 'D:\FPGA\2026.1' -CameraControl 14 -EnableObstacles -EnableWebSync -EnableManualDetection -EnablePhotoSettle -PhotoSettleMs 4000
```

Keep all feature flags above. ToF/telemetry are enabled by default; experimental ToF soft reset is disabled. `-ObstacleDebugCapture` produces a separate diagnostic ELF. Use paired BIT/XSA/PS7 hardware files and explicit user authorization for any later board operation.

## Remaining limits

This is fixed-camera foreground/motion tracking, without a semantic vehicle model. Small/distant vehicles, partial occlusions, merged objects, and initialization with already stationary objects remain difficult. Confidence is rule support, not measured probability. Confirming a new object takes multiple actual observations. Current-pixel checks deliberately clear unsupported or stale boxes. Exact vehicle tracking, end-to-end latency and complete integrated feature acceptance remain separate checks; basic R13 board performance is recorded in the linked board report.
