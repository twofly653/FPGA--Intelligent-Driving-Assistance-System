#include "xil_printf.h"
#include "xil_cache.h"
#include "xtime_l.h"
#include "sleep.h"
#include "pl_hub.h"
#include "lcd_st7789.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "stopwatch.h"
#include "input.h"
#include "attitude_est.h"
#include "telemetry.h"
#include "compass_service.h"
#include "compass_commands.h"
#include "mmc5603.h"
#include "camera_snapshot.h"
#include "traffic_service.h"
#include "obstacle_service.h"
#include "app_features.h"
#include "camera_snapshot_core.h"

/* Candidate preview default; final board acceptance may select profile 12.
 * Only low profile bits [6:1] are allowed. Bit0 is the transient reset request;
 * upper diagnostic override bits are deliberately cleared at startup. */
/* Embedded SAV/EAV synchronization reuses the same PCLK/eight data pins.
 * Build with APP_CAMERA_BT656=0 to retain the original RGB+external-sync path. */
#ifndef APP_CAMERA_BT656
#define APP_CAMERA_BT656 0
#endif
#if APP_CAMERA_BT656 != 0 && APP_CAMERA_BT656 != 1
#error "APP_CAMERA_BT656 must be0 or1"
#endif
#ifndef APP_CAMERA_STARTUP_CONTROL
#define APP_CAMERA_STARTUP_CONTROL 14
#endif
#if APP_CAMERA_STARTUP_CONTROL < 0 || APP_CAMERA_STARTUP_CONTROL > 126 || (APP_CAMERA_STARTUP_CONTROL & 1)
#error "APP_CAMERA_STARTUP_CONTROL must contain only profile bits [6:1]"
#endif

static void background_service(void)
{
    input_service();
    attitude_est_poll();
    compass_service_poll();
    compass_commands_poll();
    telemetry_poll();
}

static void camera_startup_diagnostic(void)
{
    /* Read-only camera snapshot after UI initialization; never change the
     * selected PL clock/crop profile or flood the regular TEL stream. */
    unsigned d[8];
    unsigned i;
    for (i = 0; i < 8; ++i)
        d[i] = (unsigned)pl_rd(0x58u + i * 4u);
    xil_printf("[CAMERA] d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x\r\n",
        d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    xil_printf("[CAMERA] mode=%u (0=color 1=lanes 2=gray 3=dehaze); accepted_mod256=%u display_mod4=%u\r\n",
        (d[1] >> 8) & 3u, d[3] & 255u, (d[5] >> 13) & 3u);
}

static void camera_apply_startup_control(void)
{
    /* Running this DDR-linked app establishes that PS7 initialized DDR.
     * Authorize the PL HP preview master only now, never at FPGA power-up. */
    const u32 selected = (u32)APP_CAMERA_STARTUP_CONTROL | PHOTO_CONTROL_DDR_READY |
        (APP_CAMERA_BT656 ? (1u<<13) : 0u);
    u32 identity = pl_rd(0x5Cu);
    u32 previous;

    if ((identity >> 16) != 0xCA01u) {
        xil_printf("[CAMERA] startup profile skipped: camera diagnostics unavailable\r\n");
        return;
    }
    previous = pl_rd(0x78u);
    xil_printf("[CAMERA] DDR_READY startup control=%u previous=%u\r\n",
        (unsigned)selected, (unsigned)previous);
    if (previous != selected) {
        /* Reset only camera configuration, never PS or the whole PL system.
         * An exact profile write also removes mode override bit8, restoring
         * physical BTN1/2/3 mode control after a diagnostic session. */
        pl_wr(0x78u, selected | 1u);
        usleep(10000);
        pl_wr(0x78u, selected);
    }
}

static void boot_sequence(void)
{
    lcd_set_idle_hook(0);
    seg_show_boot();
    sw_init();
    xil_printf("7-seg A + 000000\r\n");

    /* Startup/restart and navigation are quiet. Sound alerts and the ambient
     * lighting page retain their explicit functional controls. */
    ws_start(WS_MODE_OFF);

    lcd_init();
    ui_home_set_selection(UI_SEL_MENU);
    input_init();
    {
        u8 hh, mm, ss;
        lcd_defer_start();
        ui_home_draw_static();
        sw_get_time(&hh, &mm, &ss);
        ui_home_draw_clock(hh, mm, ss);
        ui_home_draw_timer_state(sw_mode());
        lcd_defer_end();
        ui_home_bind_touch();
    }
    xil_printf("LCD home up\r\n");
    ui_app_init();
    {
        mmc5603_diagnostic_t d;
        mmc5603_get_diagnostic(&d);
        xil_printf("MAG SELFTEST %s factory=%u,%u,%u threshold=%u,%u,%u status=%u polls=%u restore=%u\r\n",
            mmc5603_selftest_result_name(d.result),
            (unsigned)d.factory[0], (unsigned)d.factory[1], (unsigned)d.factory[2],
            (unsigned)d.threshold[0], (unsigned)d.threshold[1], (unsigned)d.threshold[2],
            (unsigned)d.status, d.polls, (unsigned)d.restore_ok);
    }
    lcd_set_idle_hook(background_service);
    xil_printf("HOME K1: LEFT reset / RIGHT start / UP pause / DOWN resume; CENTER=menu\r\n");
    xil_printf("Camera BTN1=Sobel lanes; BTN2=gray; BTN3=dehaze. Press active mode again for color.\r\n");
    camera_startup_diagnostic();
}

int main(void)
{
    XTime t_prev, t_now;
    u32 tick_ms = 0;
    u32 dt_ms;

    /* Framebuffer/shadow are CPU-only. Standard BSP MMU mappings leave AXI
     * peripheral registers as device memory; cache DDR for UI/EKF work. */
    Xil_DCacheEnable();
    Xil_ICacheEnable();

    /* ASCII only on UART ? UTF-8 punctuation becomes mojibake in GBK tools. */
#if APP_CAMERA_BT656
    xil_printf("\r\nCAMERA_OPTIMIZED_INVALID_BT656_BUILD / UYVY to RGB565 DDR VGA + TRAFFIC + ESKF\r\n");
#else
#if APP_ENABLE_PHOTO_SETTLE
#if APP_PHOTO_SETTLE_MIN_US == 4000000u
    xil_printf("\r\nCAMERA_SYNC_R10LCD_R2_20261028 / RGB565 DDR + MANUAL DETECTION + 5MP PHOTO SETTLE + ESKF\r\n");
#else
    xil_printf("\r\nCAMERA_SYNC_R9PHOTO_20261026 / RGB565 DDR + MANUAL DETECTION + 5MP PHOTO SETTLE + ESKF\r\n");
#endif
#elif APP_ENABLE_MANUAL_DETECTION
    xil_printf("\r\nCAMERA_SYNC_R8_20261026 / RGB565 DDR + MANUAL DETECTION + HDMI/WEB + ESKF\r\n");
#elif APP_ENABLE_WEB_SYNC
    xil_printf("\r\nCAMERA_SYNC_R7_20261026 / RGB565 DDR + HDMI/WEB SAME SOURCE + TRAFFIC + ESKF\r\n");
#elif APP_ENABLE_OBSTACLES
    xil_printf("\r\nCAMERA_OBSTACLE_R6_20261025 / RGB565 DDR VGA + MOTION BOXES + TRAFFIC + ESKF\r\n");
#else
    xil_printf("\r\nCAMERA_INTEGRATED_R5_20261025 / RGB565 DDR VGA + TRAFFIC + 5MP JPEG + ESKF\r\n");
#endif
#endif
    xil_printf("Auto boot in 2 seconds; BTN0 starts immediately.\r\n");
    xil_printf("UART 115200 8N1\r\n");

    /* Once per ARM startup. BTN0 repeats boot_sequence(), so it must not
     * reconfigure the camera or interrupt an active HDMI stream. */
    camera_apply_startup_control();
    camera_snapshot_init();
    traffic_service_init();
    obstacle_service_init();
    xil_printf("DETECT commands: OBSTACLE ON / OBSTACLE OFF (manual-control build)\r\n");
    xil_printf("PHOTO commands: PHOTO SNAP / PHOTO STATUS; JPEG 2592x1944, DDR+DAP transfer\r\n");

    seg_blank_all();

    /* Boot automatically after JTAG/power-up. BTN0 remains a restart input.
     * This avoids requiring a button press before the first screen/telemetry. */
    XTime_GetTime(&t_prev);
    while (!pl_btn0_pressed()) {
        XTime_GetTime(&t_now);
        if (t_now - t_prev >= 2ULL * COUNTS_PER_SECOND) break;
        usleep(1000);
    }
    pl_btn0_clear();
    xil_printf("Starting application\r\n");

    boot_sequence();
    xil_printf("Boot finished.\r\n");

    XTime_GetTime(&t_prev);

    while (1) {
        if (pl_btn0_pressed()) {
            pl_btn0_clear();
            xil_printf("repeat boot\r\n");
            boot_sequence();
            XTime_GetTime(&t_prev);
            tick_ms = 0;
        }

        /* Poll input twice per loop so taps are not missed during draws. */
        compass_commands_poll();
        camera_snapshot_poll();
        /* Read fresh IMU before the bounded image-copy/recognition work.
         * ui_app_poll samples again immediately afterwards. */
        attitude_est_poll();
        traffic_service_poll();
        /* Yield an IMU acquisition between independent image algorithms. */
        attitude_est_poll();
        obstacle_service_poll();
        ui_app_on_input(input_poll());
        ui_app_poll();
        ui_app_on_input(input_poll());
        lcd_poll();
        ui_app_on_input(input_poll());

        XTime_GetTime(&t_now);
        dt_ms = (u32)(((t_now - t_prev) * 1000ULL) / COUNTS_PER_SECOND);
        if (dt_ms > 0) {
            t_prev = t_now;
            tick_ms += dt_ms;
            /* Photo capture intentionally blocks UI/IMU service. Preserve
             * stopwatch wall time, then refresh once instead of replaying
             * one LCD update for every missed second. */
            dt_ms = 0;
            while (tick_ms >= 1000u) {
                tick_ms -= 1000u;
                sw_tick_1s();
                dt_ms = 1;
            }
            if (dt_ms) ui_app_on_second();
        }

        /* Short yield; long sleeps starve touch/key polling. */
        usleep(100);
    }

    return 0;
}


