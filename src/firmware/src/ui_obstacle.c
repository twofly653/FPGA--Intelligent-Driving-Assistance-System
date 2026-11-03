#include "ui_obstacle.h"
#if APP_ENABLE_MANUAL_DETECTION
#include "lcd_st7789.h"
#include "ui_draw.h"
#include "touch_hit.h"
#include "obstacle_service.h"
#include "obstacle_motion_core.h"
#include "xtime_l.h"
#include <stdio.h>
#include <string.h>

#define BG COLOR_DASH_BG
#define CARD COLOR_DASH_CARD
#define CYAN COLOR_DASH_CYAN
#define AMBER COLOR_DASH_AMBER
#define MUTED COLOR_DASH_MUTED

static unsigned g_selection, g_last_enabled;
static int g_controls_valid;
static XTime g_last_poll;
static char g_last_state[28], g_last_result[32], g_last_count[12];

static void draw_controls(unsigned enabled)
{
    ui_draw_tile(8, 8, 48, "<", "BACK", g_selection == 0u);
    ui_draw_tile(8, 68, 100, "ENABLE", enabled ? "ON" : "START", g_selection == 1u);
    ui_draw_tile(132, 68, 100, "DISABLE", enabled ? "STOP" : "OFF", g_selection == 2u);
    g_last_enabled = enabled;
    g_controls_valid = 1;
}

static void draw_values(void)
{
    obstacle_service_snapshot_t service;
    obstacle_display_snapshot_t display;
    unsigned enabled = obstacle_service_is_enabled() ? 1u : 0u;
    char state[28], result[32], count[12];
    int ready;
    obstacle_service_get_snapshot(&service);
    obstacle_service_get_display_snapshot(&display);
    ready = enabled && service.available && service.status == OBSTACLE_MOTION_READY &&
            display.available && display.status == OBSTACLE_MOTION_READY && display.age_ms <= 500u;
    (void)snprintf(state, sizeof state, "%s", enabled ? "Detection on" : "Detection off");
    if (!enabled) (void)snprintf(result, sizeof result, "Analysis paused");
    else if (!service.available) (void)snprintf(result, sizeof result, "Hardware unavailable");
    else if (service.status == OBSTACLE_MOTION_WARMUP) (void)snprintf(result, sizeof result, "Learning background");
    else if (service.status == OBSTACLE_MOTION_GLOBAL_CHANGE) (void)snprintf(result, sizeof result, "Keep camera still");
    else if (!ready) (void)snprintf(result, sizeof result, "Waiting for video");
    else (void)snprintf(result, sizeof result, "Live moving regions");
    if (ready) (void)snprintf(count, sizeof count, "%u", display.count);
    else (void)snprintf(count, sizeof count, "--");
    lcd_defer_start();
    if (!g_controls_valid || enabled != g_last_enabled) draw_controls(enabled);
    if (strcmp(state, g_last_state) || strcmp(result, g_last_result) || strcmp(count, g_last_count)) {
        lcd_fill(18, 190, 222, 245, CARD);
        ui_text(20, 190, "TARGETS", UI_FONT_SMALL, MUTED, CARD);
        ui_text(20, 211, count, UI_FONT_VALUE, ready ? CYAN : MUTED, CARD);
        ui_text(80, 194, state, UI_FONT_BODY, enabled ? CYAN : AMBER, CARD);
        ui_text(80, 219, result, UI_FONT_SMALL, MUTED, CARD);
        (void)snprintf(g_last_state, sizeof g_last_state, "%s", state);
        (void)snprintf(g_last_result, sizeof g_last_result, "%s", result);
        (void)snprintf(g_last_count, sizeof g_last_count, "%s", count);
    }
    lcd_defer_end();
}

void ui_obstacle_enter(void)
{
    unsigned enabled = obstacle_service_is_enabled() ? 1u : 0u;
    g_selection = enabled ? 1u : 2u;
    g_controls_valid = 0; g_last_poll = 0;
    g_last_state[0] = g_last_result[0] = g_last_count[0] = '\0';
    lcd_defer_start();
    ui_header("Motion", "Multi-object");
    ui_panel(8, 180, 224, 74, CYAN);
    ui_footer("Synced with web controls");
    lcd_defer_end();
    touch_hit_clear();
    touch_hit_add_square(8, 8, 48, IN_TAP, 0);
    touch_hit_add_square(8, 68, 100, IN_TAP, 1);
    touch_hit_add_square(132, 68, 100, IN_TAP, 2);
    draw_values();
}

void ui_obstacle_poll(void)
{
    XTime now;
    XTime_GetTime(&now);
    if (g_last_poll != 0 && now - g_last_poll < COUNTS_PER_SECOND / 5u) return;
    g_last_poll = now; draw_values();
}

int ui_obstacle_on_input(input_evt_t ev)
{
    int hit;
    if (ev == IN_TAP) {
        hit = touch_hit_consume_sel();
        if (hit < 0 || hit > 2) return 0;
        g_selection = (unsigned)hit; ev = IN_C;
    }
    if (ev == IN_W) return 1;
    if (ev == IN_ENC_CW) ev = IN_S;
    if (ev == IN_ENC_CCW) ev = IN_N;
    if (ev == IN_N || ev == IN_S || ev == IN_E) {
        g_selection = (g_selection + (ev == IN_N ? 2u : 1u)) % 3u;
        g_controls_valid = 0; draw_values();
    } else if (ev == IN_C || ev == IN_ENC_PB) {
        if (g_selection == 0u) return 1;
        obstacle_service_set_enabled(g_selection == 1u);
        g_controls_valid = 0; draw_values();
    }
    return 0;
}
#endif
