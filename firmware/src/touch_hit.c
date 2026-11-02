#include "touch_hit.h"
#include "lcd_st7789.h"

#define TOUCH_HIT_MAX  24

typedef struct {
    u16 x0, y0, x1, y1;
    input_evt_t ev;
    int force_sel;
} touch_hit_t;

static touch_hit_t g_hits[TOUCH_HIT_MAX];
static int g_nhit;
static int g_pending_sel = -1;
static unsigned g_generation;

void touch_hit_clear(void)
{
    g_nhit = 0;
    g_pending_sel = -1;
    ++g_generation;
}

unsigned touch_hit_generation(void) { return g_generation; }
void touch_hit_set_pending_sel(int sel) { g_pending_sel = sel; }

void touch_hit_add_ex(u16 x0, u16 y0, u16 x1, u16 y1, input_evt_t ev, int force_sel)
{
    if (g_nhit >= TOUCH_HIT_MAX || ev == IN_NONE || x0 > x1 || y0 > y1 ||
        x1 >= LCD_W || y1 >= LCD_H)
        return;
    g_hits[g_nhit].x0 = x0;
    g_hits[g_nhit].y0 = y0;
    g_hits[g_nhit].x1 = x1;
    g_hits[g_nhit].y1 = y1;
    g_hits[g_nhit].ev = ev;
    g_hits[g_nhit].force_sel = force_sel;
    g_nhit++;
}

void touch_hit_add_square(u16 x, u16 y, u16 side, input_evt_t ev, int force_sel)
{
    if (!side) return;
    touch_hit_add_ex(x, y, (u16)(x + side - 1u), (u16)(y + side - 1u), ev, force_sel);
}

void touch_hit_add(u16 x0, u16 y0, u16 x1, u16 y1, input_evt_t ev)
{
    touch_hit_add_ex(x0, y0, x1, y1, ev, -1);
}

input_evt_t touch_hit_test(u16 x, u16 y)
{
    int i;
    g_pending_sel = -1;
    for (i = 0; i < g_nhit; i++) {
        if (x >= g_hits[i].x0 && x <= g_hits[i].x1 &&
            y >= g_hits[i].y0 && y <= g_hits[i].y1) {
            g_pending_sel = g_hits[i].force_sel;
            return g_hits[i].ev;
        }
    }
    return IN_NONE;
}

int touch_hit_consume_sel(void)
{
    int s = g_pending_sel;
    g_pending_sel = -1;
    return s;
}

u16 touch_draw_pm_ex(u16 x, u16 y, u16 fc, u16 bc, int force_sel)
{
    u16 xp = (u16)(x + 56u);
    lcd_fill(x, y, (u16)(x + 48u), (u16)(y + 48u), bc);
    lcd_fill(xp, y, (u16)(xp + 48u), (u16)(y + 48u), bc);
    lcd_show_char_x2((u16)(x + 16u), (u16)(y + 8u), '-', fc, bc, 16);
    lcd_show_char_x2((u16)(xp + 16u), (u16)(y + 8u), '+', fc, bc, 16);
    touch_hit_add_square(x, y, 48, IN_W, force_sel);
    touch_hit_add_square(xp, y, 48, IN_E, force_sel);
    return (u16)(xp + 48u);
}

u16 touch_draw_pm(u16 x, u16 y, u16 fc, u16 bc)
{
    return touch_draw_pm_ex(x, y, fc, bc, -1);
}
