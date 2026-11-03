#include "ui_attitude.h"
#include "attitude_est.h"
#include "lcd_st7789.h"
#include "touch_hit.h"
#include "ui_format.h"
#include "ui_draw.h"
#include "xtime_l.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define BG COLOR_DASH_BG
#define CARD COLOR_DASH_CARD
#define CYAN COLOR_DASH_CYAN
#define AMBER COLOR_DASH_AMBER
#define MUTED COLOR_DASH_MUTED

static XTime g_last_draw;
static char g_last_values[3][16], g_last_status[48];
static int g_heading_mode, g_status_valid;
static int g_horizon_ready, g_horizon_valid, g_horizon_line;
static int g_horizon_x0, g_horizon_y0, g_horizon_x1, g_horizon_y1;

static int tenths(float degrees)
{
    float value = degrees * 10.0f;
    return (int)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

static void horizon(float roll_deg, float pitch_deg, int valid)
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, line = 0;
    float a = roll_deg * 0.01745329252f;
    int dx = (int)(cosf(a) * 88.0f), dy = (int)(sinf(a) * 88.0f);
    int lift = (int)(pitch_deg * 0.35f);
    if (valid) {
        x0 = 120 - dx; y0 = 126 + dy - lift;
        x1 = 120 + dx; y1 = 126 - dy - lift;
        line = ui_clip_line(&x0, &y0, &x1, &y1, 23, 99, 216, 153);
    }
    if (g_horizon_ready && valid == g_horizon_valid && line == g_horizon_line &&
        (!line || (x0 == g_horizon_x0 && y0 == g_horizon_y0 &&
                   x1 == g_horizon_x1 && y1 == g_horizon_y1))) return;
    /* Only the moving horizon is invalidated; static card text is retained. */
    lcd_fill(22, 99, 218, 155, CARD);
    lcd_line(28, 125, 39, 125, COLOR_DASH_BORDER);
    lcd_line(201, 125, 212, 125, COLOR_DASH_BORDER);
    lcd_line(119, 102, 119, 109, COLOR_DASH_BORDER);
    lcd_line(119, 145, 119, 152, COLOR_DASH_BORDER);
    if (valid) {
        if (line) lcd_line(x0, y0, x1, y1, CYAN);
        lcd_line(106, 126, 116, 126, AMBER);
        lcd_line(124, 126, 134, 126, AMBER);
        lcd_line(116, 126, 120, 130, AMBER);
        lcd_line(120, 130, 124, 126, AMBER);
    } else {
        ui_text_center(26, 118, 188, "Waiting for orientation", UI_FONT_SMALL, MUTED, CARD);
    }
    g_horizon_ready = 1; g_horizon_valid = valid; g_horizon_line = line;
    g_horizon_x0 = x0; g_horizon_y0 = y0; g_horizon_x1 = x1; g_horizon_y1 = y1;
}

static void cell_static(u16 x, const char *name, u16 accent)
{
    ui_panel(x, 188, 72, 62, accent);
    ui_text((u16)(x + 8), 199, name, UI_FONT_SMALL, MUTED, CARD);
}

static void cell_value(unsigned index, u16 x, const char *value)
{
    int font = ui_text_width(value, UI_FONT_TITLE) <= 60 ? UI_FONT_TITLE : UI_FONT_BODY;
    if (!strcmp(g_last_values[index], value)) return;
    lcd_fill((u16)(x + 6), 216, (u16)(x + 66), 240, CARD);
    ui_text_center((u16)(x + 6), 216, 60, value, font, COLOR_DASH_TEXT, CARD);
    (void)snprintf(g_last_values[index], sizeof g_last_values[index], "%s", value);
}

static void draw_values(void)
{
    attitude_est_snapshot_t pose;
    char r[16], p[16], y[16], progress[48];
    const char *status;
    int valid;
    attitude_est_get_snapshot(&pose);
    valid = pose.ready && pose.imu_fresh;
    if (valid) {
        ui_format_signed_tenths(r, sizeof r, tenths(pose.roll_deg));
        ui_format_signed_tenths(p, sizeof p, tenths(pose.pitch_deg));
        if (pose.heading_valid) (void)snprintf(y, sizeof y, "%03d", (int)(pose.heading_deg + 0.5f) % 360);
        else ui_format_signed_tenths(y, sizeof y, tenths(pose.yaw_deg));
    } else {
        (void)snprintf(r, sizeof r, "--"); (void)snprintf(p, sizeof p, "--");
        (void)snprintf(y, sizeof y, "--");
    }
    if (!pose.imu_detected) status = "IMU offline";
    else if (pose.aligning) {
        unsigned ms = (unsigned)(pose.warmup_elapsed_s * 1000.0f);
        if (ms > 1000u) ms = 1000u;
        (void)snprintf(progress, sizeof progress, "Hold still  %u / 1000 ms", ms);
        status = progress;
    } else if (!valid) status = "Data stale - tap ALIGN";
    else if (pose.heading_valid) status = "Magnetic heading active";
    else status = "Relative yaw - drift may occur";
    lcd_defer_start();
    horizon(pose.roll_deg, pose.pitch_deg, valid);
    if (g_heading_mode != pose.heading_valid) {
        lcd_fill(20, 160, 220, 174, CARD);
        ui_text_center(20, 160, 200, pose.heading_valid ? "9-axis orientation" : "6-axis orientation / relative", UI_FONT_SMALL, MUTED, CARD);
        lcd_fill(168, 199, 226, 212, CARD);
        ui_text(168, 199, pose.heading_valid ? "HEADING" : "YAW REL", UI_FONT_SMALL, MUTED, CARD);
        g_heading_mode = pose.heading_valid;
    }
    cell_value(0, 8, r); cell_value(1, 84, p); cell_value(2, 160, y);
    if (strcmp(g_last_status, status) || g_status_valid != valid) {
        lcd_fill(8, 260, 232, 280, BG);
        ui_text_center(8, 264, 224, status, UI_FONT_SMALL, valid ? CYAN : AMBER, BG);
        (void)snprintf(g_last_status, sizeof g_last_status, "%s", status);
        g_status_valid = valid;
    }
    lcd_defer_end();
}

void ui_attitude_enter(void)
{
    g_last_draw = 0;
    memset(g_last_values, 0, sizeof g_last_values);
    g_last_status[0] = '\0'; g_heading_mode = g_status_valid = -1;
    g_horizon_ready = 0;
    lcd_defer_start();
    ui_header("Attitude", "Live orientation");
    ui_draw_tile(184, 8, 48, "ALIGN", "STILL", 0);
    ui_panel(8, 68, 224, 112, CYAN);
    ui_text(20, 80, "ARTIFICIAL HORIZON", UI_FONT_SMALL, MUTED, CARD);
    cell_static(8, "ROLL", CYAN); cell_static(84, "PITCH", AMBER);
    cell_static(160, "YAW REL", COLOR_DASH_GREEN);
    lcd_defer_end();
    touch_hit_clear();
    touch_hit_add_square(8, 8, 48, IN_C, -1);
    touch_hit_add_square(184, 8, 48, IN_TAP, 1);
    draw_values();
    XTime_GetTime(&g_last_draw);
}

void ui_attitude_poll(void)
{
    XTime now;
    XTime_GetTime(&now);
    if (g_last_draw != 0 && now - g_last_draw < COUNTS_PER_SECOND / 5u) return;
    draw_values();
    XTime_GetTime(&g_last_draw);
}

int ui_attitude_on_input(input_evt_t ev)
{
    if (ev == IN_C || ev == IN_W || ev == IN_ENC_PB) return 1;
    if ((ev == IN_TAP && touch_hit_consume_sel() == 1) || ev == IN_E) {
        attitude_est_request_realign();
        draw_values();
        XTime_GetTime(&g_last_draw);
    }
    return 0;
}
