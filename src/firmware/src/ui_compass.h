#ifndef UI_COMPASS_H
#define UI_COMPASS_H

#include "input.h"

void ui_compass_enter(void);
void ui_compass_poll(void);
/* Returns 1 when the user requests the home page. */
int ui_compass_on_input(input_evt_t ev);

#endif
