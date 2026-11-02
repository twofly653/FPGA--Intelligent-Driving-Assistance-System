#include "input.h"
#include "pl_hub.h"
#include "cst816.h"
#include "touch_hit.h"
#include "xtime_l.h"
#include <string.h>

static input_evt_t g_event;
static int g_selection, g_touch;
static unsigned g_generation;
static XTime g_captured, g_previous_poll;
static input_stats_t g_stats;

static unsigned elapsed_us(XTime ticks)
{
    return (unsigned)((ticks * 1000000ULL) / COUNTS_PER_SECOND);
}

void input_init(void)
{
    pl_key_clear(0x1F);
    pl_enc_clear(ENC_ST_CW | ENC_ST_CCW | ENC_ST_PB);
    g_event = IN_NONE;
    g_selection = -1;
    g_touch = 0;
    g_previous_poll = 0;
    memset(&g_stats, 0, sizeof g_stats);
}

void input_service(void)
{
    u32 ks, sticky, es;
    u16 tx, ty;
    XTime now;
    unsigned gap;
    if (g_event != IN_NONE) return;
    XTime_GetTime(&now);
    gap = g_previous_poll ? elapsed_us(now - g_previous_poll) : 0;
    if (gap > g_stats.max_poll_gap_us) g_stats.max_poll_gap_us = gap;
    g_previous_poll = now;
    g_touch = 0;
    g_selection = -1;
    ks = pl_key_stat();
    sticky = (ks >> 8) & 0x1Fu;
    if (sticky & KEY_C) { pl_key_clear(KEY_C); g_event = IN_C; }
    else if (sticky & KEY_E) { pl_key_clear(KEY_E); g_event = IN_E; }
    else if (sticky & KEY_W) { pl_key_clear(KEY_W); g_event = IN_W; }
    else if (sticky & KEY_S) { pl_key_clear(KEY_S); g_event = IN_S; }
    else if (sticky & KEY_N) { pl_key_clear(KEY_N); g_event = IN_N; }
    else {
        es = pl_enc_stat();
        if (es & ENC_ST_CW) { pl_enc_clear(ENC_ST_CW); g_event = IN_ENC_CW; }
        else if (es & ENC_ST_CCW) { pl_enc_clear(ENC_ST_CCW); g_event = IN_ENC_CCW; }
        else if (es & ENC_ST_PB) { pl_enc_clear(ENC_ST_PB); g_event = IN_ENC_PB; }
        else if (cst816_poll_press(&tx, &ty)) {
            g_generation = touch_hit_generation();
            g_event = touch_hit_test(tx, ty);
            g_selection = touch_hit_consume_sel();
            g_touch = 1;
            /* One contact is one tap, including gutters. A slide must not
             * activate a different tile or a newly opened page. */
            cst816_mark_handled();
        }
    }
    if (g_event != IN_NONE) {
        XTime_GetTime(&g_captured);
        ++g_stats.events;
    }
}

input_evt_t input_poll(void)
{
    input_evt_t event;
    XTime now;
    unsigned delay;
    input_service();
    event = g_event;
    if (event == IN_NONE) return IN_NONE;
    g_event = IN_NONE;
    if (g_touch && g_generation != touch_hit_generation()) {
        ++g_stats.stale_events;
        return IN_NONE;
    }
    XTime_GetTime(&now);
    delay = elapsed_us(now - g_captured);
    if (delay > g_stats.max_dispatch_us) g_stats.max_dispatch_us = delay;
    touch_hit_set_pending_sel(g_selection);
    return event;
}

int input_pending(void) { return g_event != IN_NONE; }
void input_get_stats(input_stats_t *stats) { if (stats) *stats = g_stats; }
