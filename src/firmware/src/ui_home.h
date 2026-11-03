#ifndef UI_HOME_H
#define UI_HOME_H

#include "xil_types.h"
#include "stopwatch.h"

typedef enum {
    UI_SEL_MENU = 0
} ui_home_sel_t;

void ui_home_draw_static(void);
void ui_home_draw_clock(u8 hour, u8 min, u8 sec);
void ui_home_draw_timer_state(sw_mode_t mode);
void ui_home_draw_footer(void);
void ui_home_set_selection(ui_home_sel_t sel);
ui_home_sel_t ui_home_get_selection(void);
void ui_home_bind_touch(void);

#endif
