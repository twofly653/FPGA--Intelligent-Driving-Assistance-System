#ifndef UI_OBSTACLE_H
#define UI_OBSTACLE_H

#include "app_features.h"
#include "input.h"

#if APP_ENABLE_MANUAL_DETECTION
void ui_obstacle_enter(void);
void ui_obstacle_poll(void);
/* Returns nonzero only when the user requests the menu. */
int ui_obstacle_on_input(input_evt_t ev);
#endif

#endif
