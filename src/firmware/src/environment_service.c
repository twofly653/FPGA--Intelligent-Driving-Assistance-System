#include "environment_service.h"
#include "hdc1080.h"
#include "spa06.h"
#include "xtime_l.h"
#include <string.h>

static environment_snapshot_t g_cache;
static XTime g_hdc_start, g_hdc_next, g_hdc_fetch_next, g_spa_next, g_light_next;
static XTime g_env_stamp, g_spa_stamp, g_light_stamp;
static int g_hdc_wait, g_have_env, g_have_spa, g_have_light;

void environment_service_init(void)
{
    memset(&g_cache, 0, sizeof g_cache);
    g_cache.hdc_detected = (hdc1080_init() == 0);
    g_cache.spa_detected = (spa06_init() == 0);
    g_cache.light_detected = (veml3328_init() == 0);
    g_hdc_start = g_hdc_next = g_hdc_fetch_next = g_spa_next = g_light_next = 0;
    g_env_stamp = g_spa_stamp = g_light_stamp = 0;
    g_hdc_wait = g_have_env = g_have_spa = g_have_light = 0;
}

void environment_service_poll(void)
{
    XTime now;
    int t, rh, hpa;
    veml_sample_t light;
    XTime_GetTime(&now);
    /* One sensor operation per pass; no conversion waits in this function. */
    if (g_cache.hdc_detected && g_hdc_wait &&
        now >= g_hdc_fetch_next) {
        g_hdc_fetch_next = now + COUNTS_PER_SECOND / 200u;
        if (hdc1080_fetch(&t, &rh) == 0) {
            g_cache.temperature_x10 = t;
            g_cache.humidity_x10 = rh;
            g_env_stamp = now;
            g_have_env = 1;
            g_hdc_wait = 0;
        } else if (now - g_hdc_start >= COUNTS_PER_SECOND / 20u) {
            g_hdc_wait = 0;
        }
        return;
    }
    if (g_cache.hdc_detected && !g_hdc_wait &&
        (g_hdc_next == 0 || now >= g_hdc_next)) {
        g_hdc_next = now + COUNTS_PER_SECOND / 5u;
        if (hdc1080_start() == 0) {
            g_hdc_start = now;
            g_hdc_fetch_next = now + COUNTS_PER_SECOND / 50u;
            g_hdc_wait = 1;
        }
        return;
    }
    if (g_cache.spa_detected && (g_spa_next == 0 || now >= g_spa_next)) {
        g_spa_next = now + COUNTS_PER_SECOND / 5u;
        if (spa06_try_read_hpa(&hpa) == 0) {
            g_cache.pressure_hpa = hpa;
            g_spa_stamp = now;
            g_have_spa = 1;
        }
        return;
    }
    if (g_cache.light_detected && (g_light_next == 0 || now >= g_light_next)) {
        g_light_next = now + COUNTS_PER_SECOND / 5u;
        if (veml3328_read(&light) == 0) {
            g_cache.light = light;
            g_light_stamp = now;
            g_have_light = 1;
        }
    }
}

void environment_service_get_snapshot(environment_snapshot_t *out)
{
    XTime now;
    if (!out) return;
    XTime_GetTime(&now);
    *out = g_cache;
    out->environment_fresh = g_have_env && now - g_env_stamp <= 2u * COUNTS_PER_SECOND;
    out->pressure_fresh = g_have_spa && now - g_spa_stamp <= 2u * COUNTS_PER_SECOND;
    out->light_fresh = g_have_light && now - g_light_stamp <= 2u * COUNTS_PER_SECOND;
}
