#ifndef STOPWATCH_H
#define STOPWATCH_H

#include "xil_types.h"
#include "input.h"

typedef enum {
    SW_IDLE = 0,
    SW_RUN,
    SW_PAUSE
} sw_mode_t;

void sw_init(void);
/* Only HOME accepts direction keys. No main-board button changes the timer.
 * Return 1 if a HOME direction key was consumed, including harmless no-ops. */
int  sw_on_input(input_evt_t ev, int home_active);
void sw_tick_1s(void);        /* call once per second while running */
void sw_get_time(u8 *hh, u8 *mm, u8 *ss);
int  sw_take_dirty(void);     /* 1 if LCD should refresh HH:MM:SS */

sw_mode_t sw_mode(void);

#endif
