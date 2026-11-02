# Historical R12/R11 fixed-camera motion tracking evidence - 2026-10-29

Recorded version: `CAMERA_SYNC_R12TRACK_20261029`; the current application is R13.
See [current R13 video verification](../vehicle_tracking_video_20261029/README.md)
and the [application entry point](../../README.md). This page preserves historical
R12/R11 implementation and evidence; its results must not be relabelled as R13.
No tests, builds or board operations were performed for this documentation update.

Current R13 download and basic board checks are recorded separately in the
[2026-10-30 R13 board report](../../board_validation/r13_20261030/validation.md).

## Scope and implementation

The [motion core](../../firmware/src/obstacle_motion_core.c) and
[API contract](../../firmware/src/obstacle_motion_core.h) detect moving foreground
with a fixed camera and associate up to eight targets. Input is 320x240 RGB565,
analysis is 160x120, core boxes use 320x240 coordinates, and the service maps them
to the 640x480 display. The R12 detector state is 450328 bytes.

- Luma and two opponent-colour channels detect near-equal-luma coloured regions.
  Robust per-channel integer affine correction compensates global lighting
  changes; per-pixel noise estimates set adaptive thresholds.
- Texture-aware shadow rejection, 3x3 binary opening and connected components
  suppress sparse regions without indiscriminately merging neighbouring targets.
- New tracks require three consecutive observations, at least 80 ms, and measured
  displacement or size change before display. A missed observation interrupts
  tentative confirmation.
- Bounded assignment maximizes legal matches before minimizing their cost.
  Motion, overlap, colour and 6x6 luma/colour appearance templates gate matches.
- R12 can retain an already confirmed target after it stops, or when component
  segmentation briefly fails, only while current pixels support its foreground
  and appearance. Continuation searches nearby positions at 90/100/110% scale,
  requires at least 35% sampled foreground support and appearance error <=27.
  Prediction alone never draws a rectangle; templates update conservatively.
- Stable untracked changes can enter the background after 12 stable samples to
  reduce departed-object ghosts. Source age >500 ms or source gap >750 ms
  invalidates history; duplicate frames do not refresh the display heartbeat.

This is foreground/motion segmentation. It does not discover obstacles already
stationary during background initialization, infer object classes, or estimate
monocular distance. The 0-100 confidence value is rule support, not measured
recognition accuracy. Temporal admission adds recognition samples of latency;
detector runtime is not camera-to-display latency.

## Recorded R12 artifacts

Exact historical SHA256 values and evidence hashes are recorded in
[validation_manifest.json](build/validation_manifest.json). The release ELF links
below are mutable build destinations: the formal path now contains R13 and the
debug path may also be replaced by later builds. Their current contents do not
identify the R12 binaries listed in this table. Use the recorded hashes to
identify historical evidence; the formal R12 hash is
`722107c0d884ec4ca636a1c0686b41315edf8f261478906f65cbfa7d0fe82791`
and the diagnostic R12 hash is
`dfa94ed0fede9e9438026c2c8e3cd21548f6604a558ca20f693f1016176bff3f`.

| Artifact | Bytes | Evidence status |
| --- | ---: | --- |
| [Formal ELF](../../firmware/release/vehicle_assist_lcd_english.elf) | 1143392 | ARM build verified; download of this exact formal ELF is not evidenced |
| [Debug capture ELF](../../firmware/release/vehicle_assist_lcd_english_obstacle_debug.elf) | 1145696 | R12 diagnostic download and immutable producer captures recorded |

The [formal build log](build/tracking_formal_build.log) records entry 0x00100000,
DDR load end 0x0049E220, camera startup control 14, ToF/telemetry enabled and
experimental ToF soft reset disabled. The [debug build log](build/tracking_diagnostic_build.log)
records DDR load end 0x007FBA70. Both remain below PL preview memory at 0x10000000.

## R12 evidence

### Offline regression

[regression.json](build/regression.json) contains the recorded R12 core source SHA256
and 19 cases with no after-failures: 18 synthetic cases and one genuine 20-frame
static-camera sequence. Saved after-results contain zero false boxes, missed
targets and ID switches in their scored intervals. Synthetic acquisition frames
are excluded as recorded per case, so these numbers are not field accuracy.
Cases include eight targets, neighbours, slow/fast movement, equal-luma colours,
real-camera texture with synthetic targets, sensor noise, exposure changes,
shadows, flash/flicker and camera shake.

The [precision contract source](tests/test_precision_contract.c) now specifies
that 30 stationary samples retain the same confirmed ID before departure clears
it. [Adversarial matching](tests/test_obstacle_motion_adversarial.c) and
[service contracts](tests/test_obstacle_service.c) are also retained. This update
did not rerun them; the old README's 104-assertion and 41-dashboard-test counts
are historical reports, not newly established R12 results.

### Debug firmware on board

[tracking_download.log](build/tracking_download.log) records completion.
The associated [download script](build/download_diagnostic.tcl) selects the debug
ELF, performs system reset and PS initialization, and loads the paired stable
BIT/XSA/ps7_init files from:

`D:/project/deliveries/Task3_Submission_20261026/github/PYNQ-Z2-Vehicle-Vision/current_release`

The [UART capture](build/tracking_startup_uart.log) contains both old runtime
messages and the R12 boot. The R12 banner begins at line 1437; preceding samples
are not R12 evidence.

| Immutable debug capture | Frames | Span | Recorded result | Host replay mismatches |
| --- | ---: | ---: | --- | ---: |
| [Static ceiling](build/r12_static_ceiling/summary.json) | 20 | 4191 ms | All 20 counts are zero | 0 |
| [Vehicle sequence](build/r12_vehicle/summary.json) | 20 | 3912 ms | Counts vary from 0 to 3 | 0 |

Each capture directory retains producer input/state/metadata/box binaries,
`symbols.json`, `replay.json` and a contact sheet. The initial-state symbol is
450328 bytes. These are immutable board-analysis inputs with matching outputs;
they establish host/board repeatability. Neither sequence has user ground-truth
labels, so they do not establish recall, box tightness or real-world precision.

The formal ELF was built after the diagnostic download. Its successful build
does not mean that this exact formal binary was downloaded or visually accepted.

## Historical R11 evidence

The original manifest is preserved byte-for-byte as
[validation_manifest_r11_historical.json](build/validation_manifest_r11_historical.json).
Its source/ELF hashes describe R11, not the current files. The
[archived R11 ELF](before_tracking/vehicle_assist_r11.elf) matches its historical
ELF hash.

- [R11 download](build/download.log) and [static sample](build/static_after.json):
  73 distinct non-stale results over 14.958945 seconds, no boxes in that window,
  and 33 ms detector processing time.
- [R11 mode check](build/modes.log): colour/edge/grayscale/dehaze measured
  16.447/15.884/15.905/16.480 fps. These are not R12 performance measurements.
- An earlier R11 snapshot reported two boxes outside that controlled static
  window; its cause remains unknown. The later sample does not establish
  globally zero false positives.
- [Live DDR screenshot](build/static_live_rgb565.png) is potentially mixed-frame
  and only supports scene inspection. [startup_uart.log](build/startup_uart.log)
  predates the R11 download and is not candidate-startup evidence.

## Integrated features and remaining acceptance

HDMI RGB565 video; colour, edge, grayscale and dehaze modes; traffic-light and
countdown recognition; English extension LCD/clock; environment, relative
attitude/heading and ToF services; web synchronization; manual detection ON/OFF;
and the 5 MP still-photo path remain integrated. Their presence in the build is
separate from fresh visual acceptance. This motion record does not establish
current ToF validity, calibrated magnetic heading or complete feature acceptance.

At the time of this R12 record, pending checks were controlled moving-object accuracy/tightness/separation,
longer stopped-target retention and departure tests, formal-ELF board acceptance,
R12 mode frame rates, and measured end-to-end video/detection latency.

## Build and recovery

From the project root, the existing offline build entry point is:

```powershell
powershell -ExecutionPolicy Bypass -File .\verification\build_software.ps1 -CameraControl 14 -EnableObstacles -EnableWebSync -EnableManualDetection -EnablePhotoSettle -PhotoSettleMs 4000
```

Adding `-ObstacleDebugCapture` produces the separate debug capture ELF. This
record documents the command without executing it. Keep BIT/XSA/ps7_init from
the same hardware release. [before/](before/) retains the earlier immutable
baseline and [before_tracking/](before_tracking/) retains R11 recovery files.

## References

[reference/](reference/) retains text excerpts, original licenses and archive
SHA256 records. ODMD assumes supplied detections and known camera motion;
QD-3DT supplies ideas for association and confirmation. Neither neural runtime
nor learned model is deployed here. Reference projects were not changed and no
third-party detection implementation was copied into the firmware.
