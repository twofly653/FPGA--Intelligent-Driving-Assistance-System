#include "stopwatch.h"
#include "pl_hub.h"

static sw_mode_t g_mode = SW_IDLE;
static u8 g_hh, g_mm, g_ss;
static u8 g_dirty;

/* 7-seg state letter: A = reset (00:00:00), B = running, C = paused.
 * The digit is written on every state change and on every elapsed second so
 * the panel is a true mirror of the LCD clock instead of a boot splash. */
static void sw_show_seg(void)
{
    u8 letter = (g_mode == SW_RUN) ? 0xBu : (g_mode == SW_PAUSE) ? 0xCu : 0xAu;
    seg_show_time(letter, g_hh, g_mm, g_ss);
}

void sw_init(void)
{
    g_mode = SW_IDLE;
    g_hh = g_mm = g_ss = 0;
    g_dirty = 1;
    sw_show_seg();
}

void sw_get_time(u8 *hh, u8 *mm, u8 *ss)
{
    if (hh) *hh = g_hh;
    if (mm) *mm = g_mm;
    if (ss) *ss = g_ss;
}

int sw_take_dirty(void)
{
    int d = g_dirty;
    g_dirty = 0;
    return d;
}

void sw_tick_1s(void)
{
    if (g_mode != SW_RUN)
        return;
    g_ss++;
    if (g_ss >= 60) {
        g_ss = 0;
        g_mm++;
        if (g_mm >= 60) {
            g_mm = 0;
            if (g_hh < 99)
                g_hh++;
            else
                g_hh = 99;
        }
    }
    g_dirty = 1;
    sw_show_seg();
}

int sw_on_input(input_evt_t ev, int home_active)
{
    int changed = 0;

    /* Guard here as well as in the UI, so keys on sensor/menu pages can never
     * modify elapsed time or the running state. BTN1/2/3 belong to the camera. */
    if (!home_active)
        return 0;

    switch (ev) {
    case IN_W:                       /* A: reset to 00:00:00 */
        g_mode = SW_IDLE;
        g_hh = g_mm = g_ss = 0;
        changed = 1;
        break;
    case IN_E:                       /* B: start, or continue after pause */
    case IN_S:
        if (g_mode != SW_RUN) {
            g_mode = SW_RUN;
            changed = 1;
        }
        break;
    case IN_N:                       /* C: pause and hold the reading */
        if (g_mode == SW_RUN) {
            g_mode = SW_PAUSE;
            changed = 1;
        }
        break;
    default:
        return 0;
    }

    if (changed) {
        g_dirty = 1;
        sw_show_seg();
    }
    return 1;
}

sw_mode_t sw_mode(void)
{
    return g_mode;
}
