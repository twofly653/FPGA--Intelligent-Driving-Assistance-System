#ifndef HOST_XTIME_H
#define HOST_XTIME_H
#include <stdint.h>
typedef uint64_t XTime;
#define COUNTS_PER_SECOND 1000000ULL
extern XTime preview_ticks;
static inline void XTime_GetTime(XTime *t){*t=preview_ticks;}
#endif
