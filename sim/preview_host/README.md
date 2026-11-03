# English OLED framebuffer verification

This harness compiles the actual firmware drawing sources with `UI_HOST_PREVIEW` and reads `lcd_host_framebuffer()`. Only hardware I/O and sensor services are stubbed. Text, fonts, layout, page navigation, and drawing are executed from firmware C.

- Reproduce compilation, all framebuffer images, PNG montage, and report: `powershell -ExecutionPolicy Bypass -File render_preview.ps1`
- This uses the existing MinGW compiler and the bundled Codex Python/Pillow runtime. It does not download dependencies.
- Build alone: `powershell -ExecutionPolicy Bypass -File build_preview.ps1`
- Convert alone: run `convert_preview.py` using a Python runtime with Pillow.
- Open `index.html`, `preview-showcase.png`, or `preview-all.png`.
- Individual PPM and PNG images are in `frames/`.
- `ui_host_text_overflows` checks screen-edge text overflows and must be zero. Visual inspection is still required for card overlaps.
- Sensor values are simulated, not real hardware measurements. This verifies rendering only; no hardware is flashed or accessed.

Coverage: synced/unsynced clock, running/paused timer, all 3 menu pages, cabin data/extremes/offline, lux/raw channels/offline, IMU/live/offline, sound on/off, ambient on/off, heading/info/offline, attitude/live/alignment/offline, distance safe/warning/danger/offline, objects on/off/warmup/global-motion/offline.
