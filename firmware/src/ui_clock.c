#include "ui_clock.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>
#ifndef UI_CLOCK_HOST_TEST
#include "xtime_l.h"
static uint64_t clock_ticks(void)
{
    XTime now;
    XTime_GetTime(&now);
    return (uint64_t)now;
}
#define CLOCK_HZ ((uint64_t)COUNTS_PER_SECOND)
#else
extern uint64_t ui_clock_test_ticks(void);
#define clock_ticks ui_clock_test_ticks
#define CLOCK_HZ UINT64_C(1000000)
#endif

static int g_valid;
static uint64_t g_local_seconds;
static uint64_t g_anchor_ticks;

static int leap(unsigned y)
{
    return y % 4u == 0u && (y % 100u != 0u || y % 400u == 0u);
}

static unsigned month_days(unsigned y, unsigned m)
{
    static const unsigned days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m - 1u] + (m == 2u && leap(y) ? 1u : 0u);
}

static unsigned decimal(const char *p, unsigned count)
{
    unsigned v = 0;
    while (count--) v = v * 10u + (unsigned)(*p++ - '0');
    return v;
}

int ui_clock_command(const char *line)
{
    const char *p;
    unsigned i, y, m, d, hh, mm, ss, yr, mon;
    uint64_t days = 0;
    if (!line || strncmp(line, "TIME", 4) != 0) return 0;
    if (strlen(line) != 28u || strncmp(line, "TIME SET ", 9) != 0) return 1;
    p = line + 9;
    for (i = 0; i < 19u; ++i) {
        if (i == 4u || i == 7u) { if (p[i] != '-') return 1; }
        else if (i == 10u) { if (p[i] != 'T') return 1; }
        else if (i == 13u || i == 16u) { if (p[i] != ':') return 1; }
        else if (p[i] < '0' || p[i] > '9') return 1;
    }
    y = decimal(p, 4); m = decimal(p + 5, 2); d = decimal(p + 8, 2);
    hh = decimal(p + 11, 2); mm = decimal(p + 14, 2); ss = decimal(p + 17, 2);
    if (y < 2000u || y > 2199u || m < 1u || m > 12u || d < 1u ||
        hh > 23u || mm > 59u || ss > 59u) return 1;
    if (d > month_days(y, m)) return 1;
    for (yr = 2000u; yr < y; ++yr) days += leap(yr) ? 366u : 365u;
    for (mon = 1u; mon < m; ++mon) days += month_days(y, mon);
    days += d - 1u;
    g_local_seconds = days * UINT64_C(86400) + hh * 3600u + mm * 60u + ss;
    g_anchor_ticks = clock_ticks();
    g_valid = 1;
    return 1;
}

void ui_clock_reset(void)
{
    g_valid = 0; g_local_seconds = 0; g_anchor_ticks = 0;
}

void ui_clock_poll(void)
{
    /* Time is calculated on read, so missed UI polls never lose seconds. */
}

void ui_clock_get_snapshot(ui_clock_snapshot_t *out)
{
    uint64_t elapsed, total, days, remaining;
    unsigned y = 2000u, m = 1u;
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!g_valid) return;
    elapsed = (clock_ticks() - g_anchor_ticks) / CLOCK_HZ;
    total = g_local_seconds + elapsed;
    days = total / UINT64_C(86400);
    remaining = days;
    while (remaining >= (uint64_t)(leap(y) ? 366u : 365u)) {
        remaining -= leap(y) ? 366u : 365u; ++y;
        /* An unusable timer value must not make a UI poll unbounded. */
        if (y > 9999u) return;
    }
    while (remaining >= month_days(y, m)) {
        remaining -= month_days(y, m); ++m;
    }
    out->valid = 1; out->year = y; out->month = m; out->day = (unsigned)remaining + 1u;
    out->hour = (unsigned)(total % UINT64_C(86400) / 3600u);
    out->min = (unsigned)(total % 3600u / 60u); out->sec = (unsigned)(total % 60u);
    out->weekday = (unsigned)((days + 6u) % 7u);
    out->sync_age_seconds = elapsed > UINT_MAX ? UINT_MAX : (unsigned)elapsed;
}
