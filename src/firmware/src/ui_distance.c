#include "ui_distance.h"
#include "collision_service.h"
#include "lcd_st7789.h"
#include "touch_hit.h"
#include "ui_draw.h"
#include <stdio.h>

#define BG COLOR_DASH_BG
#define CARD COLOR_DASH_CARD
#define MUTED COLOR_DASH_MUTED
#define SAFE COLOR_DASH_GREEN
#define WARN COLOR_DASH_AMBER
#define DANGER COLOR_DASH_RED
#define BAR_X0 18
#define BAR_X1 222
#define BAR_Y0 229
#define BAR_Y1 237
#define BAR_MAX_MM 1500
#define BAR_300_X (BAR_X0 + (BAR_X1 - BAR_X0) * COLLISION_DANGER_MM / BAR_MAX_MM)
#define BAR_800_X (BAR_X0 + (BAR_X1 - BAR_X0) * COLLISION_WARNING_MM / BAR_MAX_MM)

static int g_last_ready = -1, g_last_valid = -1, g_last_mm = -1, g_last_marker = -1;
static collision_level_t g_last_level = (collision_level_t)-1;

static u16 level_color(collision_level_t level)
{
    switch (level) {
    case COLLISION_SAFE: return SAFE;
    case COLLISION_WARNING: return WARN;
    case COLLISION_DANGER: return DANGER;
    default: return MUTED;
    }
}

static int marker_x(int valid, int mm)
{
    int x;
    if (!valid) return -1;
    if (mm < 0) mm = 0;
    if (mm > BAR_MAX_MM) mm = BAR_MAX_MM;
    x = BAR_X0 + mm * (BAR_X1 - BAR_X0) / BAR_MAX_MM;
    if (x < BAR_X0 + 4) x = BAR_X0 + 4;
    if (x > BAR_X1 - 4) x = BAR_X1 - 4;
    return x;
}

static void draw_range(int valid, int mm)
{
    char digits[12];
    lcd_defer_start();
    lcd_fill(20, 101, 220, 143, CARD);
    if (valid) (void)snprintf(digits, sizeof digits, "%d", mm);
    else (void)snprintf(digits, sizeof digits, "--");
    ui_text_center(20, 102, 200, digits, UI_FONT_TIMER, valid ? COLOR_DASH_TEXT : MUTED, CARD);
    lcd_defer_end();
}

static void draw_status(collision_level_t level)
{
    const char *name = collision_level_name(level);
    u16 color = level_color(level);
    lcd_defer_start();
    ui_panel(8, 180, 224, 34, color);
    ui_text_center(16, 187, 208, name, UI_FONT_TITLE, color, CARD);
    lcd_defer_end();
}

static void draw_sensor_caption(int ready, int valid)
{
    const char *caption = !ready ? "Sensor offline" : !valid ? "Range unavailable" : "millimetres";
    lcd_defer_start();
    lcd_fill(20, 148, 220, 167, CARD);
    ui_text_center(20, 150, 200, caption, UI_FONT_SMALL, valid ? MUTED : WARN, CARD);
    lcd_defer_end();
}

static void draw_bar(int marker)
{
    lcd_defer_start();
    lcd_fill(16, 218, 224, 239, BG);
    lcd_fill(BAR_X0, BAR_Y0, BAR_300_X, BAR_Y1, DANGER);
    lcd_fill(BAR_300_X, BAR_Y0, BAR_800_X, BAR_Y1, WARN);
    lcd_fill(BAR_800_X, BAR_Y0, BAR_X1, BAR_Y1, SAFE);
    if (marker >= 0) {
        lcd_line(marker - 4, 220, marker, 225, COLOR_DASH_TEXT);
        lcd_line(marker + 4, 220, marker, 225, COLOR_DASH_TEXT);
        lcd_line(marker, 225, marker, 237, COLOR_DASH_TEXT);
    }
    lcd_defer_end();
}

void ui_distance_poll(void)
{
    const collision_model_t *state = collision_service_state();
    int ready = collision_service_sensor_ready();
    int valid = ready && state && state->valid && state->level != COLLISION_UNKNOWN;
    int mm = valid ? state->distance_mm : 0;
    int marker = marker_x(valid, mm);
    collision_level_t level = valid ? state->level : COLLISION_UNKNOWN;
    if (valid != g_last_valid || mm != g_last_mm) draw_range(valid, mm);
    if (level != g_last_level) draw_status(level);
    if (ready != g_last_ready || valid != g_last_valid) draw_sensor_caption(ready, valid);
    if (marker != g_last_marker) draw_bar(marker);
    g_last_ready = ready; g_last_valid = valid; g_last_mm = mm;
    g_last_marker = marker; g_last_level = level;
}

void ui_distance_enter(void)
{
    g_last_ready = g_last_valid = g_last_mm = -1;
    g_last_marker = -2; g_last_level = (collision_level_t)-1;
    lcd_defer_start();
    ui_header("Distance", "Front proximity");
    ui_panel(8, 68, 224, 104, COLOR_DASH_CYAN);
    ui_text(20, 80, "FRONT RANGE", UI_FONT_SMALL, MUTED, CARD);
    ui_text(18, 243, "0", UI_FONT_SMALL, MUTED, BG);
    ui_text_center(BAR_300_X - 12, 243, 24, "300", UI_FONT_SMALL, DANGER, BG);
    ui_text_center(BAR_800_X - 12, 243, 24, "800", UI_FONT_SMALL, WARN, BG);
    ui_text(190, 243, "1500+", UI_FONT_SMALL, MUTED, BG);
    ui_footer("Demo thresholds / single-point ToF");
    ui_distance_poll();
    lcd_defer_end();
    touch_hit_clear();
    touch_hit_add_square(8, 8, 48, IN_C, -1);
}

int ui_distance_on_input(input_evt_t ev)
{
    if (ev == IN_C || ev == IN_W) return 1;
    if (ev == IN_TAP && touch_hit_consume_sel() == 0) return 1;
    return 0;
}
