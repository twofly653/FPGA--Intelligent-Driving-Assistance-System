# VL53L0X R3 offline startup and reset verification

Run run_tests.ps1 on the development PC. It builds the actual production
vl53l0x.c and vl53l0x_reset.c twice with fake ST transport/API functions,
clock and delay only. It does not open a serial port or contact hardware.

## Default and experimental builds

APP_TOF_SOFT_RESET=0 is the default: preserve the original identity check and
ST initialization order without forcing reset. Build with -EnableTofSoftReset
to create the separate vehicle_assist_lcd_english_tof_soft_reset_experimental.elf.
It does not replace the default ELF. This experimental variant attempts reset
after the EEAA identity check and before DataInit.

Both variants stop initialization when either the ST API fails or the sticky
I/O flag is set. g_init_diag records stage, wrapper result, API status, I/O
state and reset diagnostics. Stages 1..16 cover identity, optional reset,
DataInit, StaticInit, reference calibration, SPAD management, limit enables and
values, measurement budget, VCSEL periods, mode, start and ready. The C header
is authoritative for exact stage numbers.

## Reset behavior

The experimental reset addresses only ToF (0x29). Each polling phase has a
100 ms deadline, 50-read cap, 2 ms interval and 5 ms transaction reserve.
A response at or beyond the deadline is rejected. Only polling reads with ST
CONTROL_INTERFACE errors permit bounded retry; failed reads cannot succeed
through a zero-filled buffer. Assertion must receive an acknowledged zero ID;
reboot must receive complete EEAA identity. Write errors remain fatal even if
best-effort reset release succeeds. The helper preserves sticky I/O evidence;
only a confirmed successful wrapper reset clears it before ST initialization.
g_reset_diag retains stage/result, first API error, read counts, NACK count
and release attempts.

Errors: -5 transport/reset write failure; -6 reset assertion timeout;
-7 reboot identity timeout. Failed startup cannot initialize or start ranging.
Unchanged runtime checks accept RangeStatus 0 only and leave caller distance
output unchanged on invalid samples.

## Verified on 2026-10-28

- Default reset-disabled build: 35 cases passed.
- Experimental reset-enabled build: 53 cases passed.
- Total: 88 cases, GCC -std=c11 -O2 -Wall -Wextra -Werror.
- Covers original ordering; initial identity, API and sticky-only errors; each
  ST initialization stage; transient/persistent NACK; reset write/cleanup
  failure; non-control API failures; full identity; slow I/O; late replies;
  count limits with stopped fake clock; NULL input; successful recovery after
  earlier errors; and invalid-range output preservation.
- Asserts unchanged 33 ms budget, 18/14 VCSEL periods, continuous mode, signal
  threshold 6554 and sigma threshold 60<<16.
- Both ARM variants compile and link. Generated diagnostic addresses match
  their ELF hashes: maximum 104 bytes default / 132 bytes experimental.

The stopped fake-clock case proves the reset helper outer read-count cap only.
It does not prove real low-level I2C completes if the global timer stops: the
production low-level delay also relies on XTime. Host tests do not validate
optical behavior or prove that a physical ToF sensor has recovered.

## Pending board acceptance

Board is unplugged and no hardware access occurred during this R3 offline
phase. On a later authorized full download, collect initialization diagnostics,
then 30 samples with an opaque target about 20-30 cm in front of ToF. Compare
valid fraction, RangeStatus, distance, return/ambient rates and UI, camera and
IMU responsiveness. Confirm default first; keep reset build experimental.
A successful startup/reset does not prove valid optical ranging. Never accept
8190/8191 or relax quality checks to hide failure.
