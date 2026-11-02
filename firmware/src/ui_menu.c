#include "ui_menu.h"
#include "lcd_st7789.h"
#include "environment_service.h"
#include "attitude_est.h"
#include "ui_format.h"
#include "ui_draw.h"
#include "i2c_bitbang.h"
#include "cst816.h"
#include "touch_hit.h"
#include "pl_hub.h"
#include "xil_printf.h"
#include "xtime_l.h"
#include <stdio.h>
#if APP_ENABLE_TOF
#include "vl53l0x_probe.h"
#include "collision_service.h"
#endif

#define BG COLOR_DASH_BG
#define CARD COLOR_DASH_CARD
#define CYAN COLOR_DASH_CYAN
#define AMBER COLOR_DASH_AMBER
#define MUTED COLOR_DASH_MUTED
#define M_BASE_ITEMS (APP_ENABLE_TOF ? 9 : 8)
#define M_ITEMS (M_BASE_ITEMS + (APP_ENABLE_MANUAL_DETECTION ? 1 : 0))
#define M_PAGES ((M_ITEMS - 2) / M_VISIBLE + 1)
#define M_VISIBLE 4
#define M_SIDE 100

static int g_menu_sel, g_light_rgb, g_detail_sel;
static environment_snapshot_t g_env;
static int g_voice_on, g_voice_armed, g_thr_db, g_db_live, g_amb_on;
static XTime g_page_draw, g_mic_tick, g_sound_hold;
static int g_menu_hit_first = -1;

/* Drawing only: sensor ownership, page events and all hit rectangles stay
 * unchanged. The shared font is proportional; every value has a bounded slot. */
static int collision_busy(void);

static void bind_back(void)
{
    touch_hit_clear();
    touch_hit_add_square(8, 8, 48, IN_C, -1);
}

static void menu_draw_full(void)
{
    static const char *names[] = { "BACK", "Cabin", "Heading", "Light",
        "Attitude", "Raw IMU", "Sound", "Ambient",
#if APP_ENABLE_TOF
        "Distance",
#endif
#if APP_ENABLE_MANUAL_DETECTION
        "Objects",
#endif
    };
    static const char *detail[] = { 0, "ENVIRONMENT", "RELATIVE", "LUX / RGB", "LIVE POSE",
        "SIX AXES", "LEVEL ALERT", "CABIN LED",
#if APP_ENABLE_TOF
        "FRONT RANGE",
#endif
#if APP_ENABLE_MANUAL_DETECTION
        "MOTION",
#endif
    };
    static const unsigned icons[] = {9, 0, 1, 2, 3, 4, 5, 6,
#if APP_ENABLE_TOF
        7,
#endif
#if APP_ENABLE_MANUAL_DETECTION
        8,
#endif
    };
    int i, first = ((g_menu_sel > 0 ? g_menu_sel - 1 : 0) / M_VISIBLE) * M_VISIBLE + 1;
    char subtitle[24];
    (void)snprintf(subtitle, sizeof subtitle, "Explore %d / %d",
                   (first - 1) / M_VISIBLE + 1, M_PAGES);
    lcd_defer_start();
    ui_header("Functions", subtitle);
    ui_draw_tile(8, 8, 48, "<", "BACK", g_menu_sel == 0);
    ui_draw_tile(184, 8, 48, ">", "PAGE", 0);
    for (i = 0; i < M_VISIBLE && first + i < M_ITEMS; ++i) {
        int item = first + i;
        u16 x = (u16)(8 + (i % 2) * 124);
        u16 y = (u16)(68 + (i / 2) * 108);
        int selected = item == g_menu_sel;
        ui_panel(x, y, M_SIDE, M_SIDE, selected ? CYAN : COLOR_DASH_BORDER);
        ui_icon((u16)(x + 40), (u16)(y + 13), icons[item], selected ? CYAN : MUTED);
        ui_text_center((u16)(x + 4), (u16)(y + 44), 92, names[item],
                       UI_FONT_BODY, COLOR_DASH_TEXT, CARD);
        ui_text_center((u16)(x + 4), (u16)(y + 70), 92, detail[item],
                       UI_FONT_SMALL, selected ? CYAN : MUTED, CARD);
        if (selected) lcd_fill((u16)(x + 45), (u16)(y + 91),
                               (u16)(x + 55), (u16)(y + 93), CYAN);
    }
    /* Keep the touch generation stable when only the focus changes. */
    if (first != g_menu_hit_first) {
        touch_hit_clear();
        touch_hit_add_square(8, 8, 48, IN_TAP, 0);
        touch_hit_add_square(184, 8, 48, IN_TAP, 100);
        for (i = 0; i < M_VISIBLE && first + i < M_ITEMS; ++i)
            touch_hit_add_square((u16)(8 + (i % 2) * 124), (u16)(68 + (i / 2) * 108),
                                 M_SIDE, IN_TAP, first + i);
        g_menu_hit_first = first;
    }
    lcd_defer_end();
}

static void menu_page_step(int direction)
{
    int current = (g_menu_sel > 0 ? g_menu_sel - 1 : 0) / M_VISIBLE;
    int next = (current + direction + M_PAGES) % M_PAGES;
    g_menu_sel = next * M_VISIBLE + 1;
    menu_draw_full();
}

static void env_draw_vals(void)
{
    char b[24];
    environment_service_get_snapshot(&g_env);
    lcd_defer_start();
    ui_panel(8, 68, 104, 108, CYAN);
    ui_text(18, 80, "TEMPERATURE", UI_FONT_SMALL, MUTED, CARD);
    if (g_env.environment_fresh) ui_format_signed_tenths(b, sizeof b, g_env.temperature_x10);
    else (void)snprintf(b, sizeof b, "--");
    ui_text_center(13, 110, 94, b,
                   ui_text_width(b, UI_FONT_VALUE) <= 94 ? UI_FONT_VALUE : UI_FONT_TITLE,
                   COLOR_DASH_TEXT, CARD);
    ui_text_center(13, 150, 94, "deg C", UI_FONT_SMALL, CYAN, CARD);
    ui_panel(128, 68, 104, 108, CYAN);
    ui_text(140, 80, "HUMIDITY", UI_FONT_SMALL, MUTED, CARD);
    if (g_env.environment_fresh)
        (void)snprintf(b, sizeof b, "%d.%d", g_env.humidity_x10 / 10, g_env.humidity_x10 % 10);
    else (void)snprintf(b, sizeof b, "--");
    ui_text_center(133, 110, 94, b,
                   ui_text_width(b, UI_FONT_VALUE) <= 94 ? UI_FONT_VALUE : UI_FONT_TITLE,
                   COLOR_DASH_TEXT, CARD);
    ui_text_center(133, 150, 94, "% RH", UI_FONT_SMALL, CYAN, CARD);
    ui_panel(8, 184, 224, 68, COLOR_DASH_BORDER);
    ui_text(20, 194, "AIR PRESSURE", UI_FONT_SMALL, MUTED, CARD);
    if (g_env.pressure_fresh) (void)snprintf(b, sizeof b, "%d", g_env.pressure_hpa);
    else (void)snprintf(b, sizeof b, "--");
    ui_text(20, 213, b, UI_FONT_VALUE, COLOR_DASH_TEXT, CARD);
    ui_text(181, 225, "hPa", UI_FONT_SMALL, CYAN, CARD);
    ui_footer(g_env.environment_fresh && g_env.pressure_fresh ?
              "Live environment" : "Waiting for sensor data");
    lcd_defer_end();
}

static void env_enter(void)
{
    lcd_defer_start(); ui_header("Cabin", "ENVIRONMENT"); lcd_defer_end();
    env_draw_vals(); bind_back();
}

static const char *light_state(int lux)
{
    if (lux < 20) return "Very low light";
    if (lux < 200) return "Dim light";
    if (lux < 1000) return "Normal light";
    return "Bright light";
}

static void light_draw_values(void)
{
    char b[28];
    int lux, i;
    environment_service_get_snapshot(&g_env);
    lux = (int)g_env.light.lux;
    lcd_defer_start();
    if (!g_light_rgb) {
        int bar = lux >= 2000 ? 200 : (lux > 0 ? lux * 200 / 2000 : 0);
        ui_panel(8, 68, 224, 112, CYAN);
        ui_text(20, 79, "AMBIENT LIGHT", UI_FONT_SMALL, MUTED, CARD);
        if (g_env.light_fresh) (void)snprintf(b, sizeof b, "%d lx", lux);
        else (void)snprintf(b, sizeof b, "-- lx");
        ui_text_center(18, 103, 204, b, UI_FONT_VALUE, COLOR_DASH_TEXT, CARD);
        ui_text_center(18, 143, 204, g_env.light_fresh ? light_state(lux) : "Sensor unavailable",
                       UI_FONT_SMALL, g_env.light_fresh ? CYAN : AMBER, CARD);
        lcd_fill(20, 167, 220, 171, COLOR_DASH_BORDER);
        if (g_env.light_fresh && bar > 0)
            lcd_fill(20, 167, (u16)(20 + bar), 171, lux < 20 ? AMBER : CYAN);
        ui_panel(8, 188, 224, 64, COLOR_DASH_BORDER);
        ui_text(20, 198, "RAW SENSOR COUNTS", UI_FONT_SMALL, MUTED, CARD);
        if (g_env.light_fresh) (void)snprintf(b, sizeof b, "G %u", (unsigned)g_env.light.g);
        else (void)snprintf(b, sizeof b, "G --");
        ui_text(20, 221, b, UI_FONT_BODY, COLOR_DASH_TEXT, CARD);
        if (g_env.light_fresh) (void)snprintf(b, sizeof b, "C %u", (unsigned)g_env.light.clear);
        else (void)snprintf(b, sizeof b, "C --");
        ui_text(128, 221, b, UI_FONT_BODY, COLOR_DASH_TEXT, CARD);
    } else {
        static const char *names[] = {"Clear", "Red", "Green", "Blue", "Infrared"};
        static const u16 accents[] = {COLOR_DASH_CYAN, COLOR_DASH_RED, COLOR_DASH_GREEN,
                                      0x6CDF, COLOR_DASH_AMBER};
        u16 raw[5];
        raw[0] = g_env.light.clear; raw[1] = g_env.light.r; raw[2] = g_env.light.g;
        raw[3] = g_env.light.b; raw[4] = g_env.light.ir;
        for (i = 0; i < 5; ++i) {
            u16 y = (u16)(70 + i * 36);
            ui_panel(8, y, 224, 32, COLOR_DASH_BORDER);
            lcd_fill(19, (u16)(y + 13), 23, (u16)(y + 19), accents[i]);
            ui_text(32, (u16)(y + 9), names[i], UI_FONT_SMALL, MUTED, CARD);
            if (g_env.light_fresh) (void)snprintf(b, sizeof b, "%u", (unsigned)raw[i]);
            else (void)snprintf(b, sizeof b, "--");
            ui_text_center(135, (u16)(y + 8), 85, b, UI_FONT_BODY, COLOR_DASH_TEXT, CARD);
        }
    }
    ui_footer(g_env.light_fresh ? (g_light_rgb ? "Raw light channels" : "Scale 0 - 2000+ lx") : "Waiting for light data");
    lcd_defer_end();
}

static void light_draw_full(void)
{
    lcd_defer_start();
    ui_header("Light", g_light_rgb ? "COLOR CHANNELS" : "ILLUMINANCE");
    ui_draw_tile(184, 8, 48, g_light_rgb ? "LUX" : "RGB", "VIEW", 0);
    lcd_defer_end();
    light_draw_values();
}

static int rounded(float value) { return (int)(value + (value >= 0.0f ? 0.5f : -0.5f)); }

static void imu_draw_values(void)
{
    lsm6_sample_t imu;
    int fresh = attitude_est_cached_imu(&imu, 0), i;
    char a[24], g[24], axis[2];
    static const char axes[] = "XYZ";
    static const u16 accents[] = {COLOR_DASH_CYAN, COLOR_DASH_AMBER, COLOR_DASH_GREEN};
    lcd_defer_start();
    for (i = 0; i < 3; ++i) {
        int mg = fresh ? rounded(imu.acc_g[i] * 1000.0f) : 0;
        unsigned magnitude = (unsigned)(mg < 0 ? -mg : mg);
        u16 y = (u16)(82 + i * 58);
        if (fresh) {
            (void)snprintf(a, sizeof a, "%c%u.%03u", mg < 0 ? '-' : '+',
                           magnitude / 1000u, magnitude % 1000u);
            ui_format_signed_tenths(g, sizeof g, rounded(imu.gyro_dps[i] * 10.0f));
        } else {
            (void)snprintf(a, sizeof a, "--");
            (void)snprintf(g, sizeof g, "--");
        }
        axis[0] = axes[i]; axis[1] = '\0';
        ui_panel(8, y, 224, 52, accents[i]);
        ui_text(20, (u16)(y + 18), axis, UI_FONT_BODY, accents[i], CARD);
        ui_text_center(44, (u16)(y + 18), 86, a, UI_FONT_BODY, COLOR_DASH_TEXT, CARD);
        ui_text_center(138, (u16)(y + 18), 86, g, UI_FONT_BODY, COLOR_DASH_TEXT, CARD);
    }
    ui_footer(fresh ? "Live sensor axes" : "IMU data unavailable");
    lcd_defer_end();
}

static void imu_enter(void)
{
    lcd_defer_start(); ui_header("Raw IMU", "SIX AXIS SENSOR");
    ui_text_center(44, 66, 86, "ACCEL (g)", UI_FONT_SMALL, MUTED, BG);
    ui_text_center(138, 66, 86, "RATE (dps)", UI_FONT_SMALL, MUTED, BG);
    lcd_defer_end(); imu_draw_values(); bind_back();
}

static void voice_live(void)
{
    char value[16];
    int bar = g_db_live > 99 ? 76 : (g_db_live > 0 ? g_db_live * 76 / 99 : 0);
    (void)snprintf(value, sizeof value, "%d", g_db_live);
    ui_panel(132, 68, 100, 100, COLOR_DASH_BORDER);
    ui_text_center(138, 80, 88, "LIVE LEVEL", UI_FONT_SMALL, MUTED, CARD);
    ui_text_center(138, 106, 88, value, UI_FONT_VALUE, COLOR_DASH_TEXT, CARD);
    ui_text_center(138, 139, 88, "approx dB", UI_FONT_SMALL, MUTED, CARD);
    lcd_fill(144, 157, 220, 160, COLOR_DASH_BORDER);
    if (bar) lcd_fill(144, 157, (u16)(144 + bar), 160, g_db_live >= g_thr_db ? AMBER : CYAN);
}

static void voice_bind(void)
{
    touch_hit_clear();
    touch_hit_add_square(8, 8, 48, IN_TAP, 0);
    touch_hit_add_square(8, 68, 100, IN_TAP, 1);
    touch_hit_add_square(8, 176, 100, IN_TAP, 2);
    touch_hit_add_square(132, 176, 100, IN_TAP, 3);
}

static void voice_draw(void)
{
    char threshold[16];
    lcd_defer_start();
    ui_header("Sound", "LEVEL ALERT");
    ui_draw_tile(8, 8, 48, "<", "BACK", g_detail_sel == 0);
    ui_draw_tile(8, 68, 100, "ALERT", g_voice_on ? "ON" : "OFF", g_detail_sel == 1);
    voice_live();
    (void)snprintf(threshold, sizeof threshold, "%d dB limit", g_thr_db);
    ui_draw_tile(8, 176, 100, "- 5 dB", threshold, g_detail_sel == 2);
    ui_draw_tile(132, 176, 100, "+ 5 dB", threshold, g_detail_sel == 3);
    lcd_defer_end();
}

static void ambient_draw(void)
{
    static const u16 spectrum[] = {COLOR_DASH_RED, COLOR_DASH_AMBER, COLOR_DASH_GREEN,
                                   COLOR_DASH_CYAN, 0x6CDF, 0xB45D};
    int i;
    lcd_defer_start(); ui_header("Ambient", "CABIN LIGHTING");
    ui_draw_tile(8, 8, 48, "<", "BACK", g_detail_sel == 0);
    ui_panel(56, 76, 128, 128, g_detail_sel == 1 ? CYAN : COLOR_DASH_BORDER);
    for (i = 0; i < 6; ++i)
        lcd_fill((u16)(72 + i * 16), 94, (u16)(84 + i * 16), 97, spectrum[i]);
    ui_icon(110, 108, 6, g_amb_on ? CYAN : MUTED);
    ui_text_center(60, 138, 120, "RAINBOW", UI_FONT_BODY, COLOR_DASH_TEXT, CARD);
    ui_text_center(60, 163, 120, g_amb_on ? "ON" : "OFF", UI_FONT_VALUE,
                   g_amb_on ? CYAN : MUTED, CARD);
    ui_text_center(8, 217, 224, "Tap to start / stop", UI_FONT_BODY, COLOR_DASH_TEXT, BG);
    ui_text_center(8, 241, 224, "3 sweeps, then auto off", UI_FONT_SMALL, MUTED, BG);
    ui_footer(collision_busy() ? "Range alert has priority" : "Decorative cabin lighting");
    lcd_defer_end();
}

static int collision_busy(void)
{
#if APP_ENABLE_TOF
    const collision_model_t *st = collision_service_state();
    return st && st->valid && st->level != COLLISION_SAFE;
#else
    return 0;
#endif
}

static void ambient_toggle(void)
{
    /* Turning it off is always allowed, even while a range alert is active:
     * that is the manual "force off" escape hatch. */
    if (g_amb_on) {
        ws_start(WS_MODE_OFF);
        g_amb_on = 0;
        ambient_draw();
        return;
    }
    if (collision_busy()) return;
    ws_set_bright(40);
    ws_start(WS_MODE_RAINBOW);
    g_amb_on = 1;
    ambient_draw();
}

void ui_sensors_init(void)
{
    u32 g;
    i2c_init();
    g = i2c_gpio_read();
    xil_printf("i2c idle gpio=0x%lx scl=%d sda=%d\r\n",
               (unsigned long)g, (int)(g & 1u), (int)((g >> 1) & 1u));
    xil_printf("ACK hdc=%d spa=%d lsm=%d veml=%d mmc=%d\r\n",
               i2c_probe(0x40) == 0, i2c_probe(0x77) == 0,
               i2c_probe(0x6B) == 0 || i2c_probe(0x6A) == 0,
               i2c_probe(0x10) == 0, i2c_probe(0x30) == 0);
#if APP_ENABLE_TOF
    xil_printf("ACK tof=%d\r\n", vl53l0x_probe() == 0);
#endif
    environment_service_init();
    environment_service_get_snapshot(&g_env);
    xil_printf("sensors hdc=%d spa=%d light=%d\r\n", g_env.hdc_detected,
               g_env.spa_detected, g_env.light_detected);
    (void)cst816_init();
    g_voice_on = g_amb_on = 0; g_voice_armed = 1; g_thr_db = 62;
    g_db_live = 0; g_page_draw = g_mic_tick = g_sound_hold = 0;
}

void ui_menu_enter(ui_page_t *page)
{
    g_menu_sel = 1; g_menu_hit_first = -1;
    *page = UI_PAGE_MENU; menu_draw_full();
}

static void back_to_menu(ui_page_t *page)
{
    *page = UI_PAGE_MENU; g_menu_hit_first = -1; menu_draw_full();
}

static void open_selected(ui_page_t *page)
{
    g_page_draw = 0;
    switch (g_menu_sel) {
    case 0: *page = UI_PAGE_HOME; break;
    case 1: *page = UI_PAGE_ENV; env_enter(); break;
    case 2: *page = UI_PAGE_COMPASS; break;
    case 3:
        *page = UI_PAGE_LIGHT; g_light_rgb = 0;
        bind_back(); touch_hit_add_square(184, 8, 48, IN_TAP, 1);
        light_draw_full(); break;
    case 4: *page = UI_PAGE_POSE; break;
    case 5: *page = UI_PAGE_IMU; imu_enter(); break;
    case 6: *page = UI_PAGE_VOICE; g_detail_sel = 0; voice_bind(); voice_draw(); break;
    case 7:
        *page = UI_PAGE_AMBIENT; g_detail_sel = 1;
        touch_hit_clear();
        touch_hit_add_square(8, 8, 48, IN_TAP, 0);
        touch_hit_add_square(56, 76, 128, IN_TAP, 1);
        ambient_draw(); break;
#if APP_ENABLE_TOF
    case 8: *page = UI_PAGE_DISTANCE; break;
#endif
#if APP_ENABLE_MANUAL_DETECTION
    case M_BASE_ITEMS: *page = UI_PAGE_OBSTACLE; break;
#endif
    default: break;
    }
}

void ui_menu_on_input(input_evt_t ev, ui_page_t *page)
{
    int hit;
    if (ev == IN_ENC_CW) ev = IN_S;
    if (ev == IN_ENC_CCW) ev = IN_N;
    if (*page == UI_PAGE_MENU) {
        if (ev == IN_TAP) {
            hit = touch_hit_consume_sel();
            if (hit == 100) {
                menu_page_step(1); return;
            }
            if (hit < 0 || hit >= M_ITEMS) return;
            g_menu_sel = hit;
            open_selected(page); return;
        }
        if (ev == IN_N || ev == IN_S) {
            g_menu_sel = (g_menu_sel + (ev == IN_S ? 1 : M_ITEMS - 1)) % M_ITEMS;
            menu_draw_full();
        } else if (ev == IN_E || ev == IN_W) {
            menu_page_step(ev == IN_E ? 1 : -1);
        } else if (ev == IN_C || ev == IN_ENC_PB) open_selected(page);
        return;
    }
    if (*page == UI_PAGE_ENV || *page == UI_PAGE_IMU) {
        if (ev == IN_C || ev == IN_W || ev == IN_ENC_PB) back_to_menu(page);
        return;
    }
    if (*page == UI_PAGE_LIGHT) {
        if (ev == IN_C || ev == IN_W) { back_to_menu(page); return; }
        if (ev == IN_TAP && touch_hit_consume_sel() == 1) {
            g_light_rgb = !g_light_rgb; light_draw_full();
        }
        return;
    }
    if (*page == UI_PAGE_VOICE) {
        if (ev == IN_TAP) {
            hit = touch_hit_consume_sel();
            if (hit < 0 || hit > 3) return;
            g_detail_sel = hit; ev = IN_C;
        }
        if (ev == IN_W) { back_to_menu(page); return; }
        if (ev == IN_N || ev == IN_S)
            g_detail_sel = (g_detail_sel + (ev == IN_S ? 1 : 3)) % 4;
        else if (ev == IN_C || ev == IN_ENC_PB) {
            if (g_detail_sel == 0) { back_to_menu(page); return; }
            if (g_detail_sel == 1) { g_voice_on = !g_voice_on; g_voice_armed = 1; }
            else {
                g_thr_db += g_detail_sel == 2 ? -5 : 5;
                if (g_thr_db < 30) g_thr_db = 30;
                if (g_thr_db > 120) g_thr_db = 120;
                g_voice_armed = 1;
            }
        } else return;
        voice_draw(); return;
    }
    if (*page == UI_PAGE_AMBIENT) {
        if (ev == IN_TAP) {
            hit = touch_hit_consume_sel();
            if (hit == 0) { back_to_menu(page); return; }
            if (hit == 1) { g_detail_sel = 1; ambient_toggle(); }
            return;
        }
        if (ev == IN_W) { back_to_menu(page); return; }
        if (ev == IN_N || ev == IN_S) {
            g_detail_sel = 1 - g_detail_sel; ambient_draw(); return;
        }
        if ((ev == IN_C || ev == IN_ENC_PB) && g_detail_sel == 0) {
            back_to_menu(page); return;
        }
        if (ev == IN_E || ev == IN_ENC_PB || ev == IN_C)
            ambient_toggle();
    }
}

/* REG_MIC_STAT.amp carries the peak |duty - 50 %| deviation of the PDM stream
 * over the last ~26 ms (0..64), so one reading is a 26 ms snapshot. Measured on
 * the board: a quiet room sits at 3..5, hand claps / speech reach 20..40.
 *
 * Two things matter for a usable meter:
 *   1. scale - 20*log10(dev) mapped onto 20..99, so silence reads ~39 instead
 *      of the old 52 and a shout lands ~89. The table is exact, a piecewise
 *      formula on an 8-bit MCU just drifts.
 *   2. ballistics - a 26 ms peak vanishes before the 5 Hz LCD refresh can draw
 *      it, so hold the peak for 300 ms and then release one step every 60 ms
 *      (a shout decays back to the floor in about 1.5 s). */
static const u8 k_dev_to_db[65] = {
    20, 26, 31, 35, 39, 42, 45, 48, 52, 54, 56, 58, 60, 62, 63, 65,
    66, 67, 69, 70, 71, 72, 73, 75, 76, 77, 78, 79, 80, 81, 82, 83,
    84, 85, 86, 86, 87, 88, 89, 90, 90, 91, 92, 92, 93, 94, 94, 95,
    96, 96, 97, 97, 98, 98, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99
};

#define MIC_HOLD_TICKS   (COUNTS_PER_SECOND * 3u / 10u)   /* 300 ms hold   */
#define MIC_DECAY_TICKS  (COUNTS_PER_SECOND / 16u)        /* 1 step/62 ms  */

static u8    g_mic_peak;
static XTime g_mic_peak_t, g_mic_decay_t;

static u8 mic_level_db(void)
{
    XTime now;
    u8 dev;

    XTime_GetTime(&now);
    dev = (u8)mic_amp();
    if (dev > 64) dev = 64;

    if (dev >= g_mic_peak) {          /* new peak -> latch and restart hold */
        g_mic_peak    = dev;
        g_mic_peak_t  = now;
        g_mic_decay_t = now;
    } else if (now - g_mic_peak_t > MIC_HOLD_TICKS) {
        if (now - g_mic_decay_t >= MIC_DECAY_TICKS) {   /* release */
            g_mic_decay_t = now;
            if (g_mic_peak > 0) g_mic_peak--;
        }
    }
    return k_dev_to_db[g_mic_peak];
}

void ui_menu_poll(ui_page_t page)
{
    XTime now; int thr_low;
    XTime_GetTime(&now);
    if (g_mic_tick == 0 || now - g_mic_tick >= COUNTS_PER_SECOND / 20u) {
        g_mic_tick = now; g_db_live = mic_level_db();
        thr_low = g_thr_db - 8;
        if (g_db_live < thr_low) g_voice_armed = 1;
        if (g_voice_on && g_voice_armed && g_db_live >= g_thr_db &&
            (g_sound_hold == 0 || now >= g_sound_hold) && !(pl_status() & ST_WS_BUSY) && !collision_busy()) {
            ws_start(WS_MODE_FLOW); g_voice_armed = 0;
            g_sound_hold = now + COUNTS_PER_SECOND / 2u;
        }
    }
    if (g_amb_on && (!(pl_status() & ST_WS_BUSY) || ws_last_mode() != WS_MODE_RAINBOW)) {
        g_amb_on = 0;
        if (page == UI_PAGE_AMBIENT) ambient_draw();
    }
    if (g_page_draw != 0 && now - g_page_draw < COUNTS_PER_SECOND / 5u) return;
    g_page_draw = now;
    if (page == UI_PAGE_ENV) env_draw_vals();
    else if (page == UI_PAGE_LIGHT) light_draw_values();
    else if (page == UI_PAGE_IMU) imu_draw_values();
    else if (page == UI_PAGE_VOICE) {
        lcd_defer_start(); voice_live(); lcd_defer_end();
    }
}
