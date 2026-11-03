# LCD civil clock and PC time sync

The English LCD candidate adds a real 24-hour civil clock. It is independent of the existing trip stopwatch.

- `src/firmware/src/ui_clock.h` exposes `ui_clock_get_snapshot()` and `ui_clock_snapshot_t` (`valid`, `year/month/day`, `hour/min/sec`, `weekday`, `sync_age_seconds`). Sunday is weekday 0.
- Power-on/CPU reset starts unsynchronized. The UI must show `--:--:--` until a valid time arrives; elapsed uptime is never presented as civil time.
- The candidate `src/dashboard/server.py` sends current PC time explicitly converted to Asia/Shanghai (UTC+8), after the first fresh board telemetry and every 60 seconds. It re-syncs after a recognized board boot or serial reconnect.
- A disconnected computer does not stop a synchronized clock: Zynq XTime advances it. This is a software clock, not a battery-backed RTC. Power loss requires another PC synchronization.
- The bounded UART line is exactly `TIME SET YYYY-MM-DDTHH:MM:SS` followed by LF (CRLF accepted by the existing parser). Years 2000 through 2199 are accepted, with Gregorian date validation. Invalid values leave the last valid clock untouched.
- The single UART owner remains `compass_commands.c`. PHOTO, compass calibration, and OBSTACLE commands keep their handlers. PC sync uses the existing serial/control locks without blocking the receive worker, and defers while a photo job is busy.
- A UI-only restart that calls `compass_commands_init()` preserves the clock. No UI polling is required to accumulate time.

The old running dashboard was not replaced. Use the **candidate** server when later deploying this firmware. Do not run two serial owners at once. This task did not connect to the board or flash firmware.

## Offline checks

`test_ui_clock.c` uses a fake 1 MHz XTime source and links the real clock and UART parser. It checks invalid/unset state, fractional-second advance, 3-day holdover without polling, date/weekday transitions, leap centuries, resynchronization, unsigned tick wrap, malformed/overlong/non-ASCII lines, and PHOTO/CAL/OBSTACLE routing.

`src/dashboard/test_clock_sync.py` tests explicit UTC+8 dates across UTC midnight, periodic writes, no replay/disconnected/stale writes, PHOTO deferral, nonblocking lock contention, boot re-sync after telemetry, and partial-write error handling. Its serial-loop test uses an in-memory fake port only.

Existing dashboard bridge, hardware-sync protocol, JPEG/photo, telemetry, and traffic protocol tests are also run. Their HTTP checks use temporary local test servers; no production dashboard or physical serial device is accessed.
