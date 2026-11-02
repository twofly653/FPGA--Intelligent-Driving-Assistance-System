#include "telemetry.h"
#include "app_features.h"
#include "xil_printf.h"
#if APP_ENABLE_TELEMETRY
#include "attitude_est.h"
#include "environment_service.h"
#include "compass_service.h"
#include "ui_format.h"
#include "traffic_service.h"
#include "obstacle_service.h"
#if APP_ENABLE_WEB_SYNC
#include "hardware_sync_protocol.h"
#include "pl_hub.h"
#endif
#include "mmc5603.h"
#include "input.h"
#include "cst816.h"
#include "lcd_st7789.h"
#include "xtime_l.h"
#include "xparameters.h"
#include "xuartps_hw.h"
#include <stdio.h>
#if APP_ENABLE_TOF
#include "collision_service.h"
#endif
static XTime g_last, g_diag_last, g_compass_last, g_traffic_last;
static char g_tx[768];
#if APP_ENABLE_WEB_SYNC
static XTime g_camera_last,g_obstacle_last;
static uint32_t g_camera_sequence;
static unsigned g_schedule_cursor;
#if APP_ENABLE_MANUAL_DETECTION
static XTime g_detect_last;
static uint32_t g_detect_sequence;
static unsigned g_detect_sent;
#endif
#define REPORT_DUE(id, fallback) (scheduled==(id))
#else
#define REPORT_DUE(id, fallback) (fallback)
#endif
static unsigned g_tx_len, g_tx_pos;
static int rounded(float x) { return (int)(x + (x >= 0.0f ? 0.5f : -0.5f)); }

static void drain_tx(void)
{
    unsigned budget = 16;
    while (g_tx_pos < g_tx_len && budget-- &&
           !XUartPs_IsTransmitFull(STDOUT_BASEADDRESS)) {
        XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_FIFO_OFFSET,
                        (unsigned char)g_tx[g_tx_pos++]);
    }
}
#endif

void telemetry_init(void)
{
#if APP_ENABLE_TELEMETRY
    g_last = g_diag_last = g_compass_last = g_traffic_last = 0;
    g_tx_len = g_tx_pos = 0;
#if APP_ENABLE_WEB_SYNC
    g_camera_last=g_obstacle_last=0;g_camera_sequence=0;g_schedule_cursor=0;
#if APP_ENABLE_MANUAL_DETECTION
    g_detect_last=0;g_detect_sequence=0;g_detect_sent=2;
#endif
#endif
    xil_printf("TELEM TEL pose=25Hz; CMP shared magnetic compass=10Hz @115200\r\n");
#else
    xil_printf("TELEM off\r\n");
#endif
}

void telemetry_flush_pending(void)
{
#if APP_ENABLE_TELEMETRY
    while(g_tx_pos<g_tx_len) drain_tx();
#endif
}

void telemetry_poll(void)
{
#if APP_ENABLE_TELEMETRY
    XTime now;
    attitude_est_snapshot_t pose;
    environment_snapshot_t env;
    int range_mm = -1, level = 0, valid, n;
    drain_tx();
    if (g_tx_pos < g_tx_len) return; /* Never queue old poses behind new ones. */
    XTime_GetTime(&now);
    attitude_est_get_snapshot(&pose);
#if APP_ENABLE_WEB_SYNC
    /* Due streams share the same nonblocking UART fairly, even when LCD/CPU
     * work reduces polling frequency. Never starve boxes behind status. */
    unsigned due=0;
    if(g_diag_last&&now-g_diag_last>=COUNTS_PER_SECOND)due|=1u;
    if(!g_traffic_last||now-g_traffic_last>=COUNTS_PER_SECOND/5u)due|=2u;
    if(!g_camera_last||now-g_camera_last>=COUNTS_PER_SECOND/5u)due|=4u;
    if(!g_obstacle_last||now-g_obstacle_last>=COUNTS_PER_SECOND/5u)due|=8u;
    if(!g_compass_last||now-g_compass_last>=COUNTS_PER_SECOND/10u)due|=16u;
    if(!g_last||now-g_last>=COUNTS_PER_SECOND/25u)due|=32u;

#if APP_ENABLE_MANUAL_DETECTION
    if(!g_detect_last||now-g_detect_last>=COUNTS_PER_SECOND/2u||g_detect_sent!=obstacle_service_is_enabled())due|=64u;
#endif
    int scheduled=hardware_sync_next_due(due,&g_schedule_cursor);
    if(scheduled<0)return;
#endif
    if (REPORT_DUE(0,g_diag_last && now - g_diag_last >= COUNTS_PER_SECOND)) {
        unsigned reads, errors, pending, slice;
        input_stats_t input;
        traffic_snapshot_t traffic_diag;
        obstacle_service_snapshot_t obstacle_diag;
        obstacle_service_get_snapshot(&obstacle_diag);
        traffic_service_get_snapshot(&traffic_diag);
        lsm6_sample_t raw = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        input_get_stats(&input);
        cst816_stats(&reads, &errors);
        lcd_get_stats(&pending, &slice);
        (void)attitude_est_cached_imu(&raw, 0);
        n = snprintf(g_tx, sizeof g_tx,
            "STAT imu=%u dt_us=%u max_dt_us=%u drop=%u imu_err=%u touch=%u touch_err=%u poll_gap_us=%u dispatch_us=%u lcd_pending=%u lcd_slice_us=%u ready=%u align=%u warm=%u gx_mdps=%d gy_mdps=%d gz_mdps=%d tr_cand=%u tr_rej=%u tr_conflict=%u tr_digit=%d tr_streak=%u tr_proc=%u tr_cap=%u tr_copy=%u ob_hw=%u ob_n=%u ob_state=%u ob_proc=%u ob_age=%u\r\n",
            pose.imu_samples, (unsigned)(pose.last_dt_s * 1000000.0f),
            (unsigned)(pose.max_dt_s * 1000000.0f), pose.dropped_samples,
            pose.imu_errors, reads, errors, input.max_poll_gap_us,
            input.max_dispatch_us, pending, slice, (unsigned)pose.ready,
            (unsigned)pose.aligning, pose.warmup_samples,
            rounded(raw.gyro_dps[0] * 1000.0f),
            rounded(raw.gyro_dps[1] * 1000.0f),
            rounded(raw.gyro_dps[2] * 1000.0f),
            traffic_diag.diagnostic_candidates,traffic_diag.diagnostic_rejected,
            traffic_diag.diagnostic_conflicts,traffic_diag.diagnostic_digit,
            traffic_diag.diagnostic_streak,(unsigned)traffic_diag.proc_ms,
            (unsigned)traffic_diag.capture_ms,(unsigned)traffic_diag.copy_ms,
            obstacle_diag.available,obstacle_diag.count,obstacle_diag.status,
            (unsigned)obstacle_diag.proc_ms,(unsigned)obstacle_diag.age_ms);
        g_diag_last = now;
    } else if (REPORT_DUE(1,!g_traffic_last || now-g_traffic_last>=COUNTS_PER_SECOND/5u)) {
        traffic_snapshot_t traffic;
        traffic_service_get_snapshot(&traffic); g_traffic_last=now;
        n=snprintf(g_tx,sizeof g_tx,"TRAFFIC %u %u %u %u %d %u %u %u\r\n",
            (unsigned)traffic.seq,traffic.color,traffic.valid,traffic.confidence,
            traffic.countdown,traffic.digit_valid,(unsigned)traffic.age_ms,(unsigned)traffic.proc_ms);
#if APP_ENABLE_WEB_SYNC
    } else if (REPORT_DUE(2,!g_camera_last || now-g_camera_last>=COUNTS_PER_SECOND/5u)) {
        camera_sync_snapshot_t camera;
        uint32_t diag=pl_rd(0x5cu),id=pl_rd(0x58u),ddr=pl_rd(0x8cu),ctrl=pl_rd(0x78u);
        camera.sequence=++g_camera_sequence;camera.mode=(diag>>8)&3u;
        camera.configured=(diag>>16)==0xca01u&&(id>>16)==0x5640u&&(diag&0x18u)==0x10u;
        camera.live=camera.configured&&(diag&1u)&&((ddr&3u)==3u)&&!(ddr&0x1f0u)&&!(ctrl&0x81u);
        camera.width=640u;camera.height=480u;g_camera_last=now;
        n=hardware_sync_camera(g_tx,sizeof g_tx,&camera);
    } else if (REPORT_DUE(3,!g_obstacle_last || now-g_obstacle_last>=COUNTS_PER_SECOND/5u)) {
        obstacle_display_snapshot_t observation;
        obstacle_service_get_display_snapshot(&observation);g_obstacle_last=now;
        n=hardware_sync_obstacles(g_tx,sizeof g_tx,&observation);
#endif
    } else if (REPORT_DUE(4,!g_compass_last || now - g_compass_last >= COUNTS_PER_SECOND / 10u)) {
        compass_service_snapshot_t compass;
        mmc5603_diagnostic_t diagnostic;
        compass_service_get_snapshot(&compass);
        mmc5603_get_diagnostic(&diagnostic);
        g_compass_last = now;
        /* The LCD COMPASS page and PC compass consume this exact shared
         * magnetic solution. TEL remains the independent ESKF pose/yaw. */
        n = snprintf(g_tx, sizeof g_tx,
            "CMP %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\r\n",
            compass.heading_deg10, compass.valid, compass.detected, compass.fresh,
            compass.calibrated, compass.aligned, compass.calibrating, compass.level,
            compass.span_x, compass.span_y, compass.coverage, compass.cal_samples,
            compass.raw_x_mg, compass.raw_y_mg, compass.raw_z_mg,
            compass.reference_deg10, compass.direction, compass.direction_verified,
            rounded(compass.fit_error * 1000000.0f), rounded(compass.tilt_deg * 10.0f),
            (int)diagnostic.result, (int)diagnostic.status);

#if APP_ENABLE_WEB_SYNC && APP_ENABLE_MANUAL_DETECTION
    } else if (REPORT_DUE(6,0)) {
        g_detect_sent=obstacle_service_is_enabled();g_detect_last=now;
        n=hardware_sync_detection(g_tx,sizeof g_tx,++g_detect_sequence,g_detect_sent);
#endif
    } else {
        if (g_last && now - g_last < COUNTS_PER_SECOND / 25u) return;
        g_last = now;
        if (!g_diag_last) g_diag_last = now;
        /* All sensor I/O belongs to acquisition services, never UART. */
        environment_service_get_snapshot(&env);
        valid = pose.ready && pose.imu_fresh;
#if APP_ENABLE_TOF
        {
            const collision_model_t *st = collision_service_state();
            if (st && st->valid) { range_mm = st->distance_mm; level = (int)st->level; }
        }
#endif
        /* Compatible 14-integer TEL frame. Last two fields are validity and
         * trusted magnetic heading. Zero angles alone never imply validity. */
        n = snprintf(g_tx, sizeof g_tx,
            "TEL %d %d %d %d %d %d %d %d %d %d %d %d %d %d\r\n",
            valid ? rounded(pose.roll_deg * 10.0f) : 0,
            valid ? rounded(pose.pitch_deg * 10.0f) : 0,
            valid ? ui_heading_degrees(pose.heading_deg) : 0,
            valid ? rounded(pose.rates_dps[0] * 10.0f) : 0,
            valid ? rounded(pose.rates_dps[1] * 10.0f) : 0,
            valid ? rounded(pose.rates_dps[2] * 10.0f) : 0,
            env.environment_fresh ? env.temperature_x10 : -9999,
            env.environment_fresh ? env.humidity_x10 : -1,
            env.pressure_fresh ? env.pressure_hpa : -1,
            env.light_fresh ? (int)env.light.lux : -1,
            range_mm, level, valid, valid && pose.heading_valid);
    }
    /* Complete frames only. Size is fixed and covers worst-case integers. */
    g_tx_len = n > 0 && (unsigned)n < sizeof g_tx ? (unsigned)n : 0;
    g_tx_pos = 0;
    drain_tx();
#endif
}
