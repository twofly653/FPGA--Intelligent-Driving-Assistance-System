#include "rtc_soft.h"

static rtc_time_t g_rtc = { 12, 0, 0 };

void rtc_init(u8 hour, u8 min, u8 sec)
{
    g_rtc.hour = hour % 24;
    g_rtc.min  = min % 60;
    g_rtc.sec  = sec % 60;
}

void rtc_set(u8 hour, u8 min, u8 sec)
{
    rtc_init(hour, min, sec);
}

void rtc_tick(void)
{
    g_rtc.sec++;
    if (g_rtc.sec >= 60) {
        g_rtc.sec = 0;
        g_rtc.min++;
        if (g_rtc.min >= 60) {
            g_rtc.min = 0;
            g_rtc.hour++;
            if (g_rtc.hour >= 24)
                g_rtc.hour = 0;
        }
    }
}

void rtc_get(rtc_time_t *t)
{
    if (t)
        *t = g_rtc;
}
