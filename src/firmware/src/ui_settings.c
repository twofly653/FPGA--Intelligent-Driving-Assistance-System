#include "ui_settings.h"
#include "ui_home.h"
#include "ui_menu.h"
#include "ui_compass.h"
#include "ui_attitude.h"
#if APP_ENABLE_MANUAL_DETECTION
#include "ui_obstacle.h"
#endif
#include "attitude_est.h"
#include "compass_service.h"
#include "compass_commands.h"
#include "telemetry.h"
#include "environment_service.h"
#include "stopwatch.h"
#include "pl_hub.h"
#include "lcd_st7789.h"
#include "xtime_l.h"
#if APP_ENABLE_TOF
#include "ui_distance.h"
#include "collision_service.h"
#endif

static ui_page_t g_page = UI_PAGE_HOME;
#if APP_ENABLE_TOF
static ui_page_t g_distance_return = UI_PAGE_MENU;
#endif

static void home_draw_drive_time(void)
{
    u8 hh, mm, ss;
    sw_get_time(&hh, &mm, &ss);
    ui_home_draw_clock(hh, mm, ss);
    ui_home_draw_timer_state(sw_mode());
}

static void enter_home(void)
{
    g_page = UI_PAGE_HOME;
    lcd_defer_start();
    ui_home_draw_static();
    home_draw_drive_time();
    lcd_defer_end();
    ui_home_bind_touch();
}

void ui_app_init(void)
{
    g_page = UI_PAGE_HOME;
    ws_set_color(WS_RED);
    ws_set_bright(40);
    seg_set_bright(50);
    ui_sensors_init();
    attitude_est_init();
    compass_service_init();
    compass_commands_init();
    lcd_set_idle_hook(attitude_est_poll);
    telemetry_init();
#if APP_ENABLE_TOF
    collision_service_init();
#endif
}

ui_page_t ui_app_page(void)
{
    return g_page;
}

void ui_app_on_second(void)
{
    if (g_page != UI_PAGE_HOME)
        return;
    home_draw_drive_time();
}

void ui_app_poll(void)
{
    /* Sample before any drawing. The LCD idle hook also services fresh IMU
     * data during long soft-SPI transfers without re-entering the UI. */
    attitude_est_poll();
    compass_service_poll();
    environment_service_poll();

    if (g_page == UI_PAGE_HOME && sw_take_dirty())
        home_draw_drive_time();

    if (g_page == UI_PAGE_COMPASS)
        ui_compass_poll();
    else if (g_page == UI_PAGE_POSE)
        ui_attitude_poll();
#if APP_ENABLE_MANUAL_DETECTION
    else if (g_page == UI_PAGE_OBSTACLE)
        ui_obstacle_poll();
#endif
#if APP_ENABLE_TOF
    else if (g_page == UI_PAGE_DISTANCE)
        ui_distance_poll();
#endif

    ui_menu_poll(g_page);

#if APP_ENABLE_TOF
    (void)collision_service_poll();
#endif

    attitude_est_poll();
    telemetry_poll();
}

void ui_app_on_input(input_evt_t ev)
{
    if (ev == IN_NONE)
        return;

    if (sw_on_input(ev, g_page == UI_PAGE_HOME))
        return;

    if (g_page == UI_PAGE_HOME) {
        if (ev == IN_TAP || ev == IN_C || ev == IN_ENC_PB) {
            ui_home_set_selection(UI_SEL_MENU);
            ui_menu_enter(&g_page);
        }
        return;
    }

    if (g_page == UI_PAGE_COMPASS) {
        if (ui_compass_on_input(ev))
            ui_menu_enter(&g_page);
        return;
    }

    if (g_page == UI_PAGE_POSE) {
        if (ui_attitude_on_input(ev))
            ui_menu_enter(&g_page);
        return;
    }

#if APP_ENABLE_MANUAL_DETECTION
    if (g_page == UI_PAGE_OBSTACLE) {
        if (ui_obstacle_on_input(ev))
            ui_menu_enter(&g_page);
        return;
    }
#endif

#if APP_ENABLE_TOF
    if (g_page == UI_PAGE_DISTANCE) {
        if (ui_distance_on_input(ev)) {
            if (g_distance_return == UI_PAGE_HOME)
                enter_home();
            else
                ui_menu_enter(&g_page);
        }
        return;
    }
#endif

    if (g_page == UI_PAGE_MENU || g_page == UI_PAGE_ENV ||
        g_page == UI_PAGE_LIGHT || g_page == UI_PAGE_IMU ||
        g_page == UI_PAGE_VOICE || g_page == UI_PAGE_AMBIENT) {
        ui_page_t p = g_page;
        ui_menu_on_input(ev, &p);
#if APP_ENABLE_TOF
        if (p == UI_PAGE_DISTANCE) {
            g_distance_return = UI_PAGE_MENU;
            g_page = UI_PAGE_DISTANCE;
            ui_distance_enter();
            return;
        }
#endif
        if (p == UI_PAGE_COMPASS) {
            g_page = UI_PAGE_COMPASS;
            ui_compass_enter();
            return;
        }
        if (p == UI_PAGE_POSE) {
            g_page = UI_PAGE_POSE;
            ui_attitude_enter();
            return;
        }
#if APP_ENABLE_MANUAL_DETECTION
        if (p == UI_PAGE_OBSTACLE) {
            g_page = UI_PAGE_OBSTACLE;
            ui_obstacle_enter();
            return;
        }
#endif
        if (p == UI_PAGE_HOME)
            enter_home();
        else
            g_page = p;
    }
}
