#ifndef RTC_SOFT_H
#define RTC_SOFT_H

#include "xil_types.h"

typedef struct {
    u8 hour;
    u8 min;
    u8 sec;
} rtc_time_t;

void rtc_init(u8 hour, u8 min, u8 sec);
void rtc_set(u8 hour, u8 min, u8 sec);
void rtc_tick(void);
void rtc_get(rtc_time_t *t);

#endif
