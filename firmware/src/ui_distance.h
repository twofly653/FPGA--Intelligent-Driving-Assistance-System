#ifndef UI_DISTANCE_H
#define UI_DISTANCE_H

#include "input.h"

/* Draw the front-ranging page and bind its BACK touch target. */
void ui_distance_enter(void);
/* Return 1 when the caller should leave the page. */
int ui_distance_on_input(input_evt_t ev);
/* Refresh only regions whose displayed state has changed. */
void ui_distance_poll(void);

#endif
