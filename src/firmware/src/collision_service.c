#include "collision_service.h"
#include "vl53l0x.h"
#include "pl_hub.h"
#include "xtime_l.h"
#include "xil_printf.h"
#include <string.h>

#define RANGE_PERIOD_MS 120u          /* measurement budget is 100 ms */
#define NO_FRAME_TIMEOUT_MS 2000u
#define RANGE_HOLD_SAMPLES 3u         /* ~360 ms of tolerance for a dropped frame */
#define IO_ERROR_STREAK_LIMIT 3u
#define RETRY_MIN_MS 1000u
#define RETRY_MAX_MS 30000u
static collision_service_diagnostic_t g_diag;
static uint32_t g_no_frame_since_ms,g_retry_started_ms,g_retry_delay_ms;
static uint32_t g_retry_backoff_ms;
static unsigned g_io_error_streak;
static unsigned g_bad_sample_streak;

static collision_model_t g_state;
static int g_ready;
static uint32_t g_last_range_ms;
static uint32_t g_last_beep_ms;
static collision_level_t g_led_level;
static u32 g_saved_color;
static u8 g_saved_bright;
static u32 g_saved_mode;
static int g_led_owned;

static uint32_t monotonic_ms(void)
{
    XTime ticks;
    XTime_GetTime(&ticks);
    return (uint32_t)(ticks / (COUNTS_PER_SECOND / 1000u));
}

static void alert_outputs(uint32_t now_ms)
{
    collision_level_t level = g_state.level;
    int active = (level == COLLISION_WARNING || level == COLLISION_DANGER);
    if (active && !g_led_owned) {
        g_saved_color = pl_rd(REG_WS_COLOR);
        g_saved_bright = (u8)pl_rd(REG_WS_BRIGHT);
        g_saved_mode = (pl_status() & ST_WS_BUSY) ?
                       ws_last_mode() : WS_MODE_OFF;
        g_led_owned = 1;
    }
    if (!active && g_led_owned) {
        ws_start(WS_MODE_OFF);
        ws_set_color(g_saved_color);
        ws_set_bright(g_saved_bright);
        if (g_saved_mode != WS_MODE_OFF)
            ws_start(g_saved_mode);
        g_led_owned = 0;
    }
    if (active && (level != g_led_level ||
                   ws_last_mode() != WS_MODE_ALLON ||
                   pl_rd(REG_WS_BRIGHT) != 55u ||
                   pl_rd(REG_WS_COLOR) !=
                       (level == COLLISION_DANGER ? WS_RED : WS_YELLOW))) {
        ws_set_color(level == COLLISION_DANGER ? WS_RED : WS_YELLOW);
        ws_set_bright(55);
        ws_start(WS_MODE_ALLON);
        if (level != g_led_level)
            /* Allow the first audible alert after a level change. */
            g_last_beep_ms = now_ms - 1500u;
    }
    g_led_level = level;
    if (level == COLLISION_DANGER &&
        (uint32_t)(now_ms - g_last_beep_ms) >= 450u) {
        buz_beep(1600, 130);
        g_last_beep_ms = now_ms;
    } else if (level == COLLISION_WARNING &&
               (uint32_t)(now_ms - g_last_beep_ms) >= 1200u) {
        buz_beep(1000, 90);
        g_last_beep_ms = now_ms;
    }
}

static void queue_recovery(collision_recovery_reason_t reason,uint32_t now)
{
    if(g_diag.recovery_pending) return;
    g_diag.recovery_pending=1;
    g_diag.recovery_reason=(int)reason;
    g_retry_started_ms=now;
    g_retry_delay_ms=g_retry_backoff_ms;
}
static void record_init_result(int result)
{
    const vl53l0x_init_diagnostic_t *d=vl53l0x_init_diagnostic();
    g_ready=(result==0);
    g_diag.last_init_result=result;
    g_diag.last_api_status=d->api_status;
    g_diag.last_io_error=d->io_error;
    if(result!=0) ++g_diag.init_failure_count;
}
static void recover_sensor(void)
{
    uint32_t now;
    int result;
    ++g_diag.recovery_count;
    g_diag.recovery_pending=0;
    /* Preserve the cabin LED ownership across sensor-only recovery. */
    collision_model_sample(&g_state,0,0,monotonic_ms());
    alert_outputs(monotonic_ms());
    result=vl53l0x_init();
    now=monotonic_ms();
    record_init_result(result);
    g_no_frame_since_ms=now;
    g_last_range_ms=now-RANGE_PERIOD_MS;
    g_io_error_streak=0;
    g_bad_sample_streak=0;
    /* A successful init is not evidence of a fresh measurement. */
    if(g_retry_backoff_ms<RETRY_MAX_MS) {
        g_retry_backoff_ms*=2u;
        if(g_retry_backoff_ms>RETRY_MAX_MS) g_retry_backoff_ms=RETRY_MAX_MS;
    }
    if(!g_ready) queue_recovery(COLLISION_RECOVERY_INIT,now);
}
void collision_service_init(void)
{
    int result;
    uint32_t now;
    const vl53l0x_init_diagnostic_t *diag;
    memset(&g_diag,0,sizeof g_diag);
    g_diag.last_return=-1;g_diag.range_status=255;g_diag.raw_mm=-1;
    g_io_error_streak=0;g_retry_backoff_ms=RETRY_MIN_MS;
    g_bad_sample_streak=0;
    g_retry_started_ms=g_retry_delay_ms=0;
    collision_model_init(&g_state);
    result=vl53l0x_init();diag=vl53l0x_init_diagnostic();
    record_init_result(result);
    now=monotonic_ms();g_no_frame_since_ms=now;
    g_last_range_ms=now-RANGE_PERIOD_MS;g_last_beep_ms=g_last_range_ms;
    g_led_level=COLLISION_UNKNOWN;g_led_owned=0;
    if(!g_ready) queue_recovery(COLLISION_RECOVERY_INIT,now);
    xil_printf("VL53L0X ranging ready=%d result=%d stage=%u api=%d io=%d reset=%d reset_io=%d nacks=%u\r\n",
               g_ready,result,diag->stage,diag->api_status,diag->io_error,
               diag->reset_enabled,diag->reset_io_seen,diag->reset_read_nacks);
}
int collision_service_poll(void)
{
    uint32_t now=monotonic_ms();
    int changed=g_state.valid;
    collision_model_tick(&g_state,now);
    changed=changed&&!g_state.valid;
    if(g_ready && (uint32_t)(now-g_last_range_ms)>=RANGE_PERIOD_MS) {
        vl53l0x_runtime_diagnostic_t d;
        int mm=0,ret=vl53l0x_read_mm(&mm);
        now=monotonic_ms();g_last_range_ms=now;
        ++g_diag.poll_count;g_diag.last_poll_ms=now;g_diag.last_return=ret;
        vl53l0x_get_runtime_diagnostic(&d);
        g_diag.last_api_status=d.api_status;g_diag.last_io_error=d.io_error;
        if(d.has_sample) {
            g_diag.range_status=d.range_status;g_diag.raw_mm=d.raw_mm;
            g_diag.signal_mcps_x65536=d.signal_mcps_x65536;
            g_diag.ambient_mcps_x65536=d.ambient_mcps_x65536;
        }
        if(d.last_read_frame && (ret==0 || ret==-3)) {
            ++g_diag.frame_count;g_diag.last_frame_ms=now;g_no_frame_since_ms=now;
            g_diag.recovery_pending=0;g_diag.recovery_reason=COLLISION_RECOVERY_NONE;
            g_retry_backoff_ms=RETRY_MIN_MS;
        }
        if(ret==-4) {
            ++g_diag.io_error_count;
            if(g_io_error_streak<IO_ERROR_STREAK_LIMIT) ++g_io_error_streak;
            if(g_io_error_streak>=IO_ERROR_STREAK_LIMIT)
                queue_recovery(COLLISION_RECOVERY_IO,now);
        } else {
            g_io_error_streak=0;
            if(ret==-2 && g_diag.recovery_pending && g_diag.recovery_reason==COLLISION_RECOVERY_IO) {
                g_diag.recovery_pending=0;g_diag.recovery_reason=COLLISION_RECOVERY_NONE;
            }
        }
        if(ret==-2) {
            int was_valid=g_state.valid;
            ++g_diag.not_ready_count;collision_model_tick(&g_state,now);
            changed|=was_valid&&!g_state.valid;
            if((uint32_t)(now-g_no_frame_since_ms)>=NO_FRAME_TIMEOUT_MS)
                queue_recovery(COLLISION_RECOVERY_NO_FRAME,now);
        } else {
            /* A single dropped frame must not blank the readout: in a bright
             * room most echoes are rejected, so clearing on every miss made
             * the display flicker instead of tracking the target. Keep the
             * last value for RANGE_HOLD_SAMPLES misses; escalation is never
             * delayed (a valid near sample still lands immediately) and
             * collision_model_tick() still expires it after COLLISION_STALE_MS. */
            if (ret == 0) {
                g_bad_sample_streak = 0;
                collision_model_sample(&g_state, 1, mm, now);
            } else if (++g_bad_sample_streak >= RANGE_HOLD_SAMPLES) {
                collision_model_sample(&g_state, 0, 0, now);
            }
            changed = 1;
            if(ret==0 && g_state.valid) {++g_diag.valid_count;g_diag.last_valid_ms=now;}
            else if(ret==-3 || ret==0) ++g_diag.invalid_count;
            else if(ret==-1) {++g_diag.not_running_count;queue_recovery(COLLISION_RECOVERY_NOT_RUNNING,now);}
        }
    }
    /* A fresh read may cancel an obsolete retry before its deadline. */
    if(g_diag.recovery_pending && (uint32_t)(now-g_retry_started_ms)>=g_retry_delay_ms) {
        recover_sensor();now=monotonic_ms();changed=1;
    }
    alert_outputs(now);
    return changed;
}
const collision_model_t *collision_service_state(void) { return &g_state; }
int collision_service_sensor_ready(void) { return g_ready; }
void collision_service_get_diagnostic(collision_service_diagnostic_t *out)
{
    uint32_t now,elapsed;
    if(!out) return;
    now=monotonic_ms();*out=g_diag;out->ready=g_ready;
    out->poll_age_ms=out->poll_count?(uint32_t)(now-out->last_poll_ms):UINT32_MAX;
    out->frame_age_ms=out->frame_count?(uint32_t)(now-out->last_frame_ms):UINT32_MAX;
    out->valid_age_ms=out->valid_count?(uint32_t)(now-out->last_valid_ms):UINT32_MAX;
    elapsed=(uint32_t)(now-g_retry_started_ms);
    out->retry_in_ms=g_diag.recovery_pending && elapsed<g_retry_delay_ms?g_retry_delay_ms-elapsed:0;
}
