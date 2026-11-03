# Integrated firmware download and verification - 2026-10-29

Firmware: CAMERA_SYNC_R10LCD_R3_20261028
ELF SHA256: f96acef27206d58148a902f277d709aabaad9700b890d421d06848d33daabee1

Full system reset, paired FPGA BIT/XSA/PS7 initialization, application download and startup completed.

Verified on connected hardware:
- Four processing modes commit successfully, continue receiving camera frames, and synchronize to the web dashboard.
- Frame rates by mode (color / Sobel / gray / dehaze): [16.469, 16.534, 16.415, 16.523] fps.
- Web detection OFF clears boxes; ON is acknowledged by the board. Original ON state restored.
- Native 2592 x 1944 JPEG photo passed CRC and full image decoding. Video and IMU recovered afterwards.
- ToF initialization completed with stage 16, result 0. Valid distances were present after download; individual invalid readings remain NO DATA.
- Clock synchronization is valid. IMU and environment telemetry are live.
- Camera control restored to 0x800E so BTN1/2/3 retain normal control.

Pending scene/manual checks:
- traffic color/countdown accuracy using a test scene
- Sobel/dehaze visual quality on target scenes
- all three ranging thresholds with known targets
- physical buttons and touch actions

Known limits:
- Magnetic heading remains uncalibrated; six-axis relative yaw is active.
- ToF invalid optical readings remain NO DATA, never a fabricated distance.
- This is a JTAG RAM download; power-off persistence was not programmed.

Display visual confirmation: user confirmed both HDMI and extension screen normal.
Dashboard: http://127.0.0.1:8767/#vehicle
Camera data/control: http://127.0.0.1:8767/#camera
Traffic test pattern: http://127.0.0.1:8767/traffic-test.html
