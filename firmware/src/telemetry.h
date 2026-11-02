#ifndef TELEMETRY_H
#define TELEMETRY_H

void telemetry_init(void);
/* Emit one JSON line for the PC dashboard (~20 Hz). */
void telemetry_poll(void);
/* Finish a queued TEL line before a command response uses the same UART TX. */
void telemetry_flush_pending(void);

#endif
