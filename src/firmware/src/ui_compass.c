#include "ui_compass.h"
#include "lcd_st7789.h"
#include "attitude_est.h"
#include "touch_hit.h"
#include "ui_draw.h"
#include "ui_format.h"
#include "xtime_l.h"
#include "stdio.h"
#include "math.h"

/* Relative heading dial: only the small pointer and value are refreshed. */
#define CX 120
#define CY 162
#define R_OUT 73
#define R_TICK 68
/* The pointer stays inside the ticks, outside the central value. */
#define R_TRI_TIP   55
#define R_TRI_BASE  47
#define TRI_HALF_W  6
#define PI_F 3.14159265f

#define C_BG    COLOR_DASH_BG
#define C_CARD  COLOR_DASH_CARD
#define C_CYAN  COLOR_DASH_CYAN
#define C_AMBER COLOR_DASH_AMBER
#define C_MUTED COLOR_DASH_MUTED
#define C_WHITE COLOR_DASH_TEXT

static int g_info_page, g_force_redraw, g_last_status, g_last_heading;
static int g_dial_ready, g_heading, g_heading_valid;
static int g_needle_deg = -1;
static XTime g_last_draw_tick;

static void pix(int x, int y, u16 c)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H)
        return;
    lcd_fill((u16)x, (u16)y, (u16)(x + 1), (u16)(y + 1), c);
}

static void circle(int cx, int cy, int r, u16 c)
{
    int x = -r, y = 0, err = 2 - 2 * r;
    do {
        pix(cx - x, cy + y, c);
        pix(cx - y, cy - x, c);
        pix(cx + x, cy - y, c);
        pix(cx + y, cy + x, c);
        r = err;
        if (r <= y) err += ++y * 2 + 1;
        if (r > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
}

static void draw_static_dial(void)
{
    int d;
    ui_header("Heading", "Relative yaw");
    ui_draw_tile(184, 8, 48, "INFO", "VIEW", 0);
    ui_panel(8, 68, 224, 190, C_CYAN);
    circle(CX, CY, R_OUT, COLOR_DASH_BORDER);
    for (d = 0; d < 360; d += 5) {
        float a = (float)d * PI_F / 180.0f;
        int major = d % 30 == 0;
        int r0 = R_TICK - (major ? 8 : 3);
        int x0 = CX + (int)(sinf(a) * (float)r0);
        int y0 = CY - (int)(cosf(a) * (float)r0);
        int x1 = CX + (int)(sinf(a) * (float)R_TICK);
        int y1 = CY - (int)(cosf(a) * (float)R_TICK);
        lcd_line(x0, y0, x1, y1, major ? C_CYAN : C_MUTED);
    }
    ui_text_center(104, 75, 32, "0", UI_FONT_SMALL, C_WHITE, C_CARD);
    ui_text_center(200, 156, 30, "90", UI_FONT_SMALL, C_WHITE, C_CARD);
    ui_text_center(100, 241, 40, "180", UI_FONT_SMALL, C_WHITE, C_CARD);
    ui_text_center(10, 156, 30, "270", UI_FONT_SMALL, C_WHITE, C_CARD);
    ui_footer("Relative yaw / not magnetic north");
    g_dial_ready = 1;
}

/* Zero is the shared attitude startup reference, not magnetic north. */
static void tri_verts(int deg, int *xt, int *yt, int *xl, int *yl, int *xr, int *yr)
{
    float a = (float)deg * PI_F / 180.0f;
    float sa = sinf(a);
    float ca = cosf(a);
    int xb = CX + (int)(sa * (float)R_TRI_BASE);
    int yb = CY - (int)(ca * (float)R_TRI_BASE);

    *xt = CX + (int)(sa * (float)R_TRI_TIP);
    *yt = CY - (int)(ca * (float)R_TRI_TIP);
    *xl = xb + (int)(ca * (float)TRI_HALF_W);
    *yl = yb + (int)(sa * (float)TRI_HALF_W);
    *xr = xb - (int)(ca * (float)TRI_HALF_W);
    *yr = yb - (int)(sa * (float)TRI_HALF_W);
}

static void tri_bbox(int deg, int *x0, int *y0, int *x1, int *y1)
{
    int xt, yt, xl, yl, xr, yr;
    int minx, miny, maxx, maxy;

    tri_verts(deg, &xt, &yt, &xl, &yl, &xr, &yr);
    minx = xt; if (xl < minx) minx = xl; if (xr < minx) minx = xr;
    maxx = xt; if (xl > maxx) maxx = xl; if (xr > maxx) maxx = xr;
    miny = yt; if (yl < miny) miny = yl; if (yr < miny) miny = yr;
    maxy = yt; if (yl > maxy) maxy = yl; if (yr > maxy) maxy = yr;
    *x0 = minx - 2;
    *y0 = miny - 2;
    *x1 = maxx + 2;
    *y1 = maxy + 2;
}

static void erase_heading_tri(int deg)
{
    int x0, y0, x1, y1;

    tri_bbox(deg, &x0, &y0, &x1, &y1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= LCD_W) x1 = LCD_W - 1;
    if (y1 >= LCD_H) y1 = LCD_H - 1;
    /* Card fill only in annulus — does not reach outer ring / ticks. */
    lcd_fill((u16)x0, (u16)y0, (u16)x1, (u16)y1, C_CARD);

}

static void draw_heading_tri(int deg, u16 fill, u16 edge)
{
    int xt, yt, xl, yl, xr, yr;
    int y, miny, maxy;

    tri_verts(deg, &xt, &yt, &xl, &yl, &xr, &yr);

    miny = yt; if (yl < miny) miny = yl; if (yr < miny) miny = yr;
    maxy = yt; if (yl > maxy) maxy = yl; if (yr > maxy) maxy = yr;

    for (y = miny; y <= maxy; y++) {
        int xs[3], n = 0, i, j, xL, xR;
        int y0 = yt, y1 = yl, x0 = xt, x1 = xl;
        if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y)) {
            if (y1 != y0)
                xs[n++] = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        }
        y0 = yl; y1 = yr; x0 = xl; x1 = xr;
        if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y)) {
            if (y1 != y0)
                xs[n++] = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        }
        y0 = yr; y1 = yt; x0 = xr; x1 = xt;
        if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y)) {
            if (y1 != y0)
                xs[n++] = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        }
        if (n < 2)
            continue;
        xL = xs[0];
        xR = xs[0];
        for (i = 1; i < n; i++) {
            if (xs[i] < xL) xL = xs[i];
            if (xs[i] > xR) xR = xs[i];
        }
        if (xL > xR) { j = xL; xL = xR; xR = j; }
        if (xL < 0) xL = 0;
        if (xR >= LCD_W) xR = LCD_W - 1;
        if (xR >= xL)
            lcd_fill((u16)xL, (u16)y, (u16)(xR + 1), (u16)(y + 1), fill);
    }

    lcd_line(xt, yt, xl, yl, edge);
    lcd_line(xl, yl, xr, yr, edge);
    lcd_line(xr, yr, xt, yt, edge);
}

static void draw_heading_needle(int heading)
{
    if (g_needle_deg >= 0)
        erase_heading_tri(g_needle_deg);
    draw_heading_tri(heading, C_AMBER, C_CYAN);
    g_needle_deg = heading;
}

static void draw_center(int heading, int status)
{
    char value[8];
    lcd_fill(85, 133, 155, 193, C_CARD);
    ui_text_center(85, 134, 70, "RELATIVE", UI_FONT_SMALL, C_MUTED, C_CARD);
    if (status == 3) snprintf(value, sizeof(value), "%03d", heading);
    else snprintf(value, sizeof(value), "--");
    ui_text_center(85, 150, 70, value, UI_FONT_VALUE,
                   status == 3 ? C_WHITE : C_MUTED, C_CARD);
    ui_text_center(85, 181, 70, "degrees", UI_FONT_SMALL, C_CYAN, C_CARD);
}

static void bind_touch(void)
{
    touch_hit_clear();
    touch_hit_add_square(8, 8, 48, IN_C, -1);
    if (!g_info_page) touch_hit_add_square(184, 8, 48, IN_TAP, 1);
}

static void read_heading(void)
{
    attitude_est_snapshot_t pose;
    attitude_est_get_snapshot(&pose);
    g_heading = ui_heading_degrees(pose.heading_deg);
    g_heading_valid = pose.ready && pose.imu_fresh && g_heading >= 0;
}

static void draw_info(void)
{
    lcd_defer_start();
    ui_header("Heading", "How it works");
    ui_panel(8, 68, 224, 52, C_CYAN);
    ui_text(20, 78, "One shared heading", UI_FONT_BODY, C_WHITE, C_CARD);
    ui_text(20, 101, "Screen and web stay in sync", UI_FONT_SMALL, C_MUTED, C_CARD);
    ui_panel(8, 128, 224, 52, C_CYAN);
    ui_text(20, 138, "Startup reference", UI_FONT_BODY, C_WHITE, C_CARD);
    ui_text(20, 161, "Zero is set when aligned", UI_FONT_SMALL, C_MUTED, C_CARD);
    ui_panel(8, 188, 224, 64, C_AMBER);
    ui_text(20, 198, "Relative direction", UI_FONT_BODY, C_WHITE, C_CARD);
    ui_text(20, 222, "Yaw may drift over time", UI_FONT_SMALL, C_MUTED, C_CARD);
    ui_text(20, 237, "Not a magnetic-north compass", UI_FONT_SMALL, C_AMBER, C_CARD);
    ui_footer("Back returns to the heading dial");
    lcd_defer_end();
    bind_touch();
}

static void draw_current(void)
{
    int status = g_heading_valid ? 3 : 0;
    lcd_defer_start();
    if (!g_dial_ready) draw_static_dial();
    if (g_last_status != status || g_force_redraw) {
        lcd_fill(68, 36, 178, 52, C_BG);
        ui_text(68, 36, g_heading_valid ? "Relative yaw" : "Waiting for IMU",
                UI_FONT_SMALL, g_heading_valid ? C_MUTED : C_AMBER, C_BG);
    }
    if (g_heading_valid && (g_heading != g_last_heading || g_force_redraw))
        draw_heading_needle(g_heading);
    else if (!g_heading_valid && g_needle_deg >= 0) {
        erase_heading_tri(g_needle_deg);
        g_needle_deg = -1;
    }
    draw_center(g_heading, status);
    lcd_defer_end();
    g_last_status = status;
    g_last_heading = g_heading_valid ? g_heading : -1;
    g_force_redraw = 0;
    XTime_GetTime(&g_last_draw_tick);
}

void ui_compass_enter(void)
{
    g_info_page = 0; g_dial_ready = 0; g_needle_deg = -1;
    g_last_heading = g_last_status = -1; g_force_redraw = 1;
    g_last_draw_tick = 0;
    read_heading(); bind_touch(); draw_current();
}

void ui_compass_poll(void)
{
    XTime now;
    int status, heading;
    if (g_info_page) return;
    XTime_GetTime(&now); read_heading();
    status = g_heading_valid ? 3 : 0;
    heading = g_heading_valid ? g_heading : -1;
    if (g_force_redraw || status != g_last_status ||
        (heading != g_last_heading && now - g_last_draw_tick >= COUNTS_PER_SECOND / 5u))
        draw_current();
}

int ui_compass_on_input(input_evt_t ev)
{
    if (ev == IN_C || ev == IN_ENC_PB || ev == IN_W) {
        if (g_info_page) { ui_compass_enter(); return 0; }
        return 1;
    }
    if (!g_info_page && (ev == IN_S || ev == IN_E ||
        (ev == IN_TAP && touch_hit_consume_sel() == 1))) {
        g_info_page = 1; draw_info();
    }
    return 0;
}
