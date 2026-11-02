#include "ui_home.h"
#include "ui_draw.h"
#include "ui_clock.h"
#include "touch_hit.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

static char g_last_clock[16];
static char g_last_date[32];
static char g_last_trip[16];
static int g_last_mode = -1;

static void draw_menu(void)
{
    ui_panel(156, 184, 72, 72, COLOR_DASH_CYAN);
    ui_icon(182, 197, 9, COLOR_DASH_CYAN);
    ui_text_center(156, 229, 72, "MENU", UI_FONT_BODY,
                   COLOR_DASH_TEXT, COLOR_DASH_CARD);
}

void ui_home_draw_footer(void)
{
    lcd_defer_start();
    draw_menu();
    ui_footer("L Reset  R Start  U Pause  D Resume");
    lcd_defer_end();
}

void ui_home_set_selection(ui_home_sel_t sel) { (void)sel; }
ui_home_sel_t ui_home_get_selection(void) { return UI_SEL_MENU; }

void ui_home_draw_static(void)
{
    g_last_clock[0] = g_last_date[0] = g_last_trip[0] = '\0';
    g_last_mode = -1;
    lcd_defer_start();
    lcd_fill(0, 0, LCD_W, LCD_H, COLOR_DASH_BG);
    ui_icon(15, 20, 4, COLOR_DASH_CYAN);
    ui_text(47, 11, "DRIVESENSE", UI_FONT_TITLE, COLOR_DASH_TEXT, COLOR_DASH_BG);
    ui_text(48, 37, "SMART MOBILITY", UI_FONT_SMALL, COLOR_DASH_MUTED, COLOR_DASH_BG);
    lcd_line(12, 61, 228, 61, COLOR_DASH_BORDER);
    ui_panel(12, 72, 216, 100, COLOR_DASH_BORDER);
    ui_text(26, 83, "LOCAL TIME", UI_FONT_SMALL, COLOR_DASH_CYAN, COLOR_DASH_CARD);
    ui_text(172, 83, "UTC+8", UI_FONT_SMALL, COLOR_DASH_MUTED, COLOR_DASH_CARD);
    ui_panel(12, 184, 132, 72, COLOR_DASH_BORDER);
    ui_text(24, 193, "TRIP TIMER", UI_FONT_SMALL, COLOR_DASH_MUTED, COLOR_DASH_CARD);
    draw_menu();
    ui_footer("L Reset  R Start  U Pause  D Resume");
    lcd_defer_end();
}

/* Public arguments are the existing elapsed stopwatch. Wall time is separate
 * and comes from the PC-synchronized monotonic civil clock. */
void ui_home_draw_clock(u8 hour, u8 min, u8 sec)
{
    static const char *const days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    ui_clock_snapshot_t now;
    char clock[16], date[32], trip[16];
    ui_clock_get_snapshot(&now);
    if (now.valid) {
        (void)snprintf(clock, sizeof(clock), "%02u:%02u:%02u", now.hour, now.min, now.sec);
        (void)snprintf(date, sizeof(date), "%s  %04u.%02u.%02u", days[now.weekday % 7u],
                       now.year, now.month, now.day);
    } else {
        (void)snprintf(clock, sizeof(clock), "--:--:--");
        (void)snprintf(date, sizeof(date), "WAITING FOR PC SYNC");
    }
    (void)snprintf(trip, sizeof(trip), "%02u:%02u:%02u", hour, min, sec);
    lcd_defer_start();
    if (strcmp(clock, g_last_clock)) {
        lcd_fill(20, 101, 220, 142, COLOR_DASH_CARD);
        ui_text_center(20, 101, 200, clock, UI_FONT_TIMER,
                       now.valid ? COLOR_DASH_TEXT : COLOR_DASH_MUTED, COLOR_DASH_CARD);
        (void)strcpy(g_last_clock, clock);
    }
    if (strcmp(date, g_last_date)) {
        lcd_fill(20, 147, 220, 161, COLOR_DASH_CARD);
        ui_text_center(20, 147, 200, date, UI_FONT_SMALL, COLOR_DASH_MUTED, COLOR_DASH_CARD);
        (void)strcpy(g_last_date, date);
    }
    if (strcmp(trip, g_last_trip)) {
        lcd_fill(23, 210, 134, 231, COLOR_DASH_CARD);
        ui_text(24, 210, trip, UI_FONT_TITLE, COLOR_DASH_TEXT, COLOR_DASH_CARD);
        (void)strcpy(g_last_trip, trip);
    }
    lcd_defer_end();
}

void ui_home_draw_timer_state(sw_mode_t mode)
{
    /* Same letters as the 7-seg state digit: A reset, B running, C paused. */
    const char *label = mode == SW_RUN ? "B  RUNNING"
                      : mode == SW_PAUSE ? "C  PAUSED" : "A  READY";
    u16 color = mode == SW_RUN ? COLOR_DASH_CYAN : mode == SW_PAUSE ? COLOR_DASH_AMBER : COLOR_DASH_MUTED;
    if (g_last_mode == (int)mode) return;
    lcd_defer_start();
    lcd_fill(23, 236, 134, 250, COLOR_DASH_CARD);
    lcd_fill(25, 241, 29, 245, color);
    ui_text(36, 236, label, UI_FONT_SMALL, color, COLOR_DASH_CARD);
    lcd_defer_end();
    g_last_mode = (int)mode;
}

void ui_home_bind_touch(void)
{
    touch_hit_clear();
    touch_hit_add_square(156, 184, 72, IN_TAP, 0);
}
