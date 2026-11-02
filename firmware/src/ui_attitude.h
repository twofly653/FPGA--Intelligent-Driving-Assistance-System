#ifndef UI_ATTITUDE_H
#define UI_ATTITUDE_H

#include "input.h"

void ui_attitude_enter(void);
void ui_attitude_poll(void);
/* 1 = leave page */
int ui_attitude_on_input(input_evt_t ev);

#endif
