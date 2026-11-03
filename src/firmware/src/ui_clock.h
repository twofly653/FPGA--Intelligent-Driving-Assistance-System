#ifndef UI_CLOCK_H
#define UI_CLOCK_H

/* Civil time is Asia/Shanghai (UTC+8), supplied explicitly by the PC.
 * XTime advances it after synchronization; no battery-backed RTC is implied.
 * CPU/board restart starts unsynchronized. A UI-only restart preserves it. */
typedef struct {
    int valid;
    unsigned year, month, day;
    unsigned hour, min, sec;
    unsigned weekday; /* 0 Sunday .. 6 Saturday */
    unsigned sync_age_seconds;
} ui_clock_snapshot_t;

void ui_clock_poll(void);
void ui_clock_get_snapshot(ui_clock_snapshot_t *out);
/* Recognize TIME-prefixed input; malformed input never changes the clock.
 * Accepted form is exactly TIME SET YYYY-MM-DDTHH:MM:SS (2000..2199). */
int ui_clock_command(const char *line);
/* Clear only for an intentional clock reset (or host tests). */
void ui_clock_reset(void);

#endif
