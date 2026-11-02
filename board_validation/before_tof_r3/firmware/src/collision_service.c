#include "collision_service.h"
#include "vl53l0x.h"
#include "pl_hub.h"
#include "xtime_l.h"
#include "xil_printf.h"

#define RANGE_PERIOD_MS 200u

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

void collision_service_init(void)
{
    collision_model_init(&g_state);
    g_ready = (vl53l0x_init() == 0);
    g_last_range_ms = monotonic_ms() - RANGE_PERIOD_MS;
    g_last_beep_ms = g_last_range_ms;
    g_led_level = COLLISION_UNKNOWN;
    g_led_owned = 0;
    xil_printf("VL53L0X ranging init=%d\r\n", g_ready);
}

int collision_service_poll(void)
{
    uint32_t now_ms = monotonic_ms();
    int mm = 0;
    int ret;
    int stale_changed = g_state.valid;
    collision_model_tick(&g_state, now_ms);
    stale_changed = stale_changed && !g_state.valid;
    if (!g_ready) {
        alert_outputs(now_ms);
        return stale_changed;
    }
    if ((uint32_t)(now_ms - g_last_range_ms) < RANGE_PERIOD_MS) {
        alert_outputs(now_ms);
        return stale_changed;
    }
    ret = vl53l0x_read_mm(&mm);
    now_ms = monotonic_ms();
    g_last_range_ms = now_ms; /* Leave room for touch/UI even if I2C was slow. */
    if (ret == -2) { /* A conversion is still in progress; stale timer remains active. */
        int was_valid = g_state.valid;
        collision_model_tick(&g_state, now_ms);
        alert_outputs(now_ms);
        return stale_changed || (was_valid && !g_state.valid);
    }
    collision_model_sample(&g_state, ret == 0, mm, now_ms);
    /* Runtime status is carried by the shared nonblocking TEL channel.
     * Direct printf here could split a TEL frame currently being drained. */
    alert_outputs(now_ms);
    return 1;
}

const collision_model_t *collision_service_state(void)
{
    return &g_state;
}

int collision_service_sensor_ready(void)
{
    return g_ready;
}
