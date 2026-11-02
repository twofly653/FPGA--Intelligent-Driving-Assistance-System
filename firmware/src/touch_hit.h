#ifndef TOUCH_HIT_H
#define TOUCH_HIT_H

#include "xil_types.h"
#include "input.h"

void touch_hit_clear(void);
unsigned touch_hit_generation(void);
void touch_hit_set_pending_sel(int sel);
void touch_hit_add(u16 x0, u16 y0, u16 x1, u16 y1, input_evt_t ev);
/* force_sel >= 0: UI should select that row before applying ev */
void touch_hit_add_ex(u16 x0, u16 y0, u16 x1, u16 y1, input_evt_t ev, int force_sel);
/* Square controls use the same half-open bounds as their visible LCD fill. */
void touch_hit_add_square(u16 x, u16 y, u16 side, input_evt_t ev, int force_sel);
input_evt_t touch_hit_test(u16 x, u16 y);
int touch_hit_consume_sel(void); /* -1 if none */

/* Draw "-" / "+" ; if force_sel>=0, taps set that selection first. */
u16 touch_draw_pm_ex(u16 x, u16 y, u16 fc, u16 bc, int force_sel);
u16 touch_draw_pm(u16 x, u16 y, u16 fc, u16 bc);

#endif
