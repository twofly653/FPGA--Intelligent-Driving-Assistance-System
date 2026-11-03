#include "attitude_est.h"
#include "attitude_ekf.h"
#include "attitude_board_config.h"
#include "xtime_l.h"
#include <math.h>
#include <string.h>

#define DEG_TO_RAD 0.017453292519943295f
#define PI_F 3.14159265358979323846f
#define IMU_FRESH_S 0.25f
#define MAG_FRESH_S 0.50f
#define WARM_MIN_S 1.0f
#define WARM_MIN_N 50u
#define WARM_MAX_GAP_S 0.04f
#define IMU_ODR_HZ 104.0f
#define NO_AGE_S 1000000.0f

static attitude_ekf_t g_ekf;
static attitude_ekf_output_t g_out;
static lsm6_sample_t g_imu;
static mmc5603_sample_t g_mag;
static int g_imu_detected, g_mag_detected, g_ready, g_aligning;
static int g_mag_pending, g_imu_failures, g_mag_failures;
static XTime g_imu_tick, g_mag_tick, g_step_tick;
static XTime g_imu_attempt, g_mag_attempt, g_mag_fetch_tick;
static XTime g_imu_probe_tick, g_mag_probe_tick, g_mag_start_tick;
static XTime g_warm_start;
static unsigned g_imu_n, g_mag_n, g_drop_n, g_imu_errors, g_mag_errors;
static unsigned g_consumed_mag, g_warm_mag_sequence, g_warm_n, g_warm_mag_n;
static float g_last_dt, g_max_dt, g_warm_elapsed;
static float g_acc_mean[3], g_gyro_mean[3], g_acc_m2[3], g_gyro_m2[3];
static float g_first_acc_dir[3], g_mag_mean[3];

static float seconds(XTime ticks)
{
    return (float)((double)ticks / (double)COUNTS_PER_SECOND);
}

static float age(XTime now, XTime tick, unsigned count)
{
    return count ? seconds(now - tick) : NO_AGE_S;
}

static float norm3(const float v[3])
{
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

static int finite3(const float v[3])
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static void map3(const float matrix[3][3], const float in[3], float out[3])
{
    int i;
    for (i = 0; i < 3; ++i)
        out[i] = matrix[i][0]*in[0] + matrix[i][1]*in[1] + matrix[i][2]*in[2];
}

static int calibrated_mag(float body[3])
{
    float raw[3], centered[3], corrected[3], length;
    int i;
    raw[0] = (float)g_mag.x_mg;
    raw[1] = (float)g_mag.y_mg;
    raw[2] = (float)g_mag.z_mg;
    for (i = 0; i < 3; ++i)
        centered[i] = raw[i] - attitude_mag_hard_iron_mg[i];
    map3(attitude_mag_soft_iron, centered, corrected);
    map3(attitude_mag_to_body, corrected, body);
    length = norm3(body);
    /* Coarse physical plausibility is not proof of interference freedom. */
    return finite3(body) && length >= 100.0f && length <= 1000.0f;
}

static void reset_window(void)
{
    g_warm_n = g_warm_mag_n = 0;
    g_warm_elapsed = 0.0f;
    g_warm_start = 0;
    g_warm_mag_sequence = g_mag_n;
    memset(g_acc_mean, 0, sizeof g_acc_mean);
    memset(g_gyro_mean, 0, sizeof g_gyro_mean);
    memset(g_acc_m2, 0, sizeof g_acc_m2);
    memset(g_gyro_m2, 0, sizeof g_gyro_m2);
    memset(g_mag_mean, 0, sizeof g_mag_mean);
}

void attitude_est_request_realign(void)
{
    g_ready = 0;
    g_aligning = 1;
    reset_window();
}

void attitude_est_init(void)
{
    memset(&g_ekf, 0, sizeof g_ekf);
    memset(&g_out, 0, sizeof g_out);
    memset(&g_imu, 0, sizeof g_imu);
    memset(&g_mag, 0, sizeof g_mag);
    g_imu_detected = lsm6_init() == 0;
    g_mag_detected = mmc5603_init() == 0;
    g_mag_pending = g_imu_failures = g_mag_failures = 0;
    g_imu_tick = g_mag_tick = g_step_tick = 0;
    g_imu_attempt = g_mag_attempt = g_mag_fetch_tick = 0;
    g_imu_probe_tick = g_mag_probe_tick = g_mag_start_tick = 0;
    g_imu_n = g_mag_n = g_drop_n = g_imu_errors = g_mag_errors = 0;
    g_consumed_mag = 0;
    g_last_dt = g_max_dt = 0.0f;
    attitude_est_request_realign();
}

static void poll_magnetometer(XTime now)
{
    int result;
    mmc5603_sample_t next;
    if (!g_mag_detected) {
        if (!mmc5603_reprobe_allowed()) return;
        if (now - g_mag_probe_tick < 5u * COUNTS_PER_SECOND)
            return;
        g_mag_probe_tick = now;
        g_mag_detected = mmc5603_init() == 0;
        g_mag_failures = 0;
        if (!g_mag_detected)
            return;
    }
    if (g_mag_pending) {
        if (now - g_mag_fetch_tick < COUNTS_PER_SECOND / 400u)
            return;
        g_mag_fetch_tick = now;
        result = mmc5603_try_fetch(&next);
        if (result == 0) {
            g_mag = next;
            g_mag_tick = now;
            ++g_mag_n;
            g_mag_pending = 0;
            g_mag_failures = 0;
        } else if (result < 0 || seconds(now - g_mag_start_tick) > 0.03f) {
            g_mag_pending = 0;
            ++g_mag_errors;
            if (++g_mag_failures >= 3) {
                g_mag_detected = 0;
                g_mag_probe_tick = now;
            }
        }
        return;
    }
    if (g_mag_attempt != 0 && now - g_mag_attempt < COUNTS_PER_SECOND / 12u)
        return;
    g_mag_attempt = now; /* Rate limit failures as well as successes. */
    if (mmc5603_start() == 0) {
        g_mag_pending = 1;
        g_mag_start_tick = g_mag_fetch_tick = now;
    } else {
        ++g_mag_errors;
        if (++g_mag_failures >= 3) {
            g_mag_detected = 0;
            g_mag_probe_tick = now;
        }
    }
}

static void collect_stationary(XTime now, const float acc[3], const float gyro[3])
{
    float a_len = norm3(acc), g_len = norm3(gyro), variance_a = 0.0f;
    float variance_g = 0.0f, alignment = 0.0f, mag_body[3];
    const float *mag0 = 0;
    int i;
    if (!finite3(acc) || !finite3(gyro) || fabsf(a_len - 1.0f) > 0.06f ||
        g_len > ATTITUDE_ALIGN_GYRO_LIMIT_DPS ||
        (g_warm_n && g_last_dt > WARM_MAX_GAP_S)) {
        reset_window();
        return;
    }
    if (g_warm_n) {
        for (i = 0; i < 3; ++i)
            alignment += g_first_acc_dir[i] * acc[i] / a_len;
        if (alignment < 0.9986295f) { /* >3 degree direction change */
            reset_window();
            return;
        }
    } else {
        g_warm_start = now;
        for (i = 0; i < 3; ++i)
            g_first_acc_dir[i] = acc[i] / a_len;
    }
    ++g_warm_n;
    for (i = 0; i < 3; ++i) {
        float da = acc[i] - g_acc_mean[i];
        float dg = gyro[i] - g_gyro_mean[i];
        g_acc_mean[i] += da / (float)g_warm_n;
        g_gyro_mean[i] += dg / (float)g_warm_n;
        g_acc_m2[i] += da * (acc[i] - g_acc_mean[i]);
        g_gyro_m2[i] += dg * (gyro[i] - g_gyro_mean[i]);
        variance_a += g_acc_m2[i];
        variance_g += g_gyro_m2[i];
    }
    if (g_warm_n >= 10 &&
        (variance_a / (float)(g_warm_n - 1) > 0.0004f ||
         variance_g / (float)(g_warm_n - 1) > 0.16f)) {
        reset_window();
        return;
    }
    g_warm_elapsed = seconds(now - g_warm_start);
    if (ATTITUDE_BOARD_MAG_CALIBRATED && g_mag_n != g_warm_mag_sequence &&
        age(now, g_mag_tick, g_mag_n) <= 0.2f && calibrated_mag(mag_body)) {
        g_warm_mag_sequence = g_mag_n;
        ++g_warm_mag_n;
        for (i = 0; i < 3; ++i)
            g_mag_mean[i] += (mag_body[i] - g_mag_mean[i]) / (float)g_warm_mag_n;
    }
    if (g_warm_elapsed < WARM_MIN_S || g_warm_n < WARM_MIN_N)
        return;
    if (ATTITUDE_BOARD_MAG_CALIBRATED && g_warm_mag_n >= 3 &&
        age(now, g_mag_tick, g_mag_n) <= 0.2f)
        mag0 = g_mag_mean;
    if (!attitude_ekf_init(&g_ekf, 0, g_acc_mean, mag0)) {
        reset_window();
        return;
    }
    attitude_ekf_set_gyro_bias_dps(&g_ekf, g_gyro_mean);
    (void)attitude_ekf_get_output(&g_ekf, &g_out);
    g_step_tick = now;
    g_consumed_mag = g_mag_n;
    g_ready = 1;
    g_aligning = 0;
}

void attitude_est_poll(void)
{
    XTime now;
    lsm6_sample_t next;
    float acc[3], gyro[3], mag_body[3];
    const float *mag_ptr = 0;
    int result;
    XTime_GetTime(&now);
    poll_magnetometer(now);
    if (!g_imu_detected) {
        if (now - g_imu_probe_tick < 2u * COUNTS_PER_SECOND)
            return;
        g_imu_probe_tick = now;
        g_imu_detected = lsm6_init() == 0;
        g_imu_failures = 0;
        if (!g_imu_detected)
            return;
        attitude_est_request_realign();
    }
    if (g_imu_attempt != 0 && now - g_imu_attempt < COUNTS_PER_SECOND / 400u)
        return;
    g_imu_attempt = now;
    result = lsm6_try_read(&next);
    if (result == 1)
        return;
    if (result < 0) {
        ++g_imu_errors;
        if (g_aligning)
            reset_window();
        if (++g_imu_failures >= 3) {
            g_imu_detected = 0;
            g_imu_probe_tick = now;
        }
        return;
    }
    /* Timestamp successful coherent reads; never manufacture a shorter dt. */
    XTime_GetTime(&now);
    g_last_dt = g_imu_n ? seconds(now - g_imu_tick) : 0.0f;
    if (g_last_dt > g_max_dt)
        g_max_dt = g_last_dt;
    if (g_imu_n && g_last_dt * IMU_ODR_HZ >= 1.5f) {
        unsigned periods = (unsigned)(g_last_dt * IMU_ODR_HZ + 0.5f);
        g_drop_n += periods - 1u;
    }
    g_imu = next;
    g_imu_tick = now;
    ++g_imu_n;
    g_imu_failures = 0;
    map3(attitude_imu_to_body, next.acc_g, acc);
    map3(attitude_imu_to_body, next.gyro_dps, gyro);
    if (g_aligning) {
        collect_stationary(now, acc, gyro);
        return;
    }
    if (!g_ready)
        return;
    if (ATTITUDE_BOARD_MAG_CALIBRATED && g_mag_n != g_consumed_mag &&
        age(now, g_mag_tick, g_mag_n) <= 0.2f) {
        g_consumed_mag = g_mag_n;
        if (calibrated_mag(mag_body))
            mag_ptr = mag_body;
    }
    if (attitude_ekf_step(&g_ekf, gyro, acc, mag_ptr, g_last_dt)) {
        g_step_tick = now;
    } else {
        ++g_drop_n;
        if (!isfinite(g_last_dt) || g_last_dt <= 0.0f ||
            g_last_dt > g_ekf.config.max_dt_s) {
            /* A latest register value cannot reconstruct the missing motion. */
            g_ready = 0;
            g_aligning = 0; /* require an explicit stationary realignment */
        }
    }
    (void)attitude_ekf_get_output(&g_ekf, &g_out);
}

void attitude_est_get_snapshot(attitude_est_snapshot_t *s)
{
    XTime now;
    float body[3];
    int i;
    if (!s)
        return;
    XTime_GetTime(&now);
    memset(s, 0, sizeof *s);
    s->imu_detected = g_imu_detected;
    s->mag_detected = g_mag_detected;
    s->imu_age_s = age(now, g_imu_tick, g_imu_n);
    s->mag_age_s = age(now, g_mag_tick, g_mag_n);
    s->imu_fresh = s->imu_age_s <= IMU_FRESH_S;
    s->mag_fresh = s->mag_age_s <= MAG_FRESH_S;
    s->ready = g_ready && s->imu_fresh && seconds(now - g_step_tick) <= IMU_FRESH_S;
    s->aligning = g_aligning;
    s->mag_fusion_enabled = ATTITUDE_BOARD_MAG_CALIBRATED;
    s->roll_pitch_recent = s->ready && g_out.roll_pitch_recent;
    s->heading_valid = s->ready && s->mag_fresh && g_out.heading_recent;
    s->roll_deg = g_out.roll_deg;
    s->pitch_deg = g_out.pitch_deg;
    s->yaw_deg = g_out.yaw_deg;
    s->heading_deg = g_out.compass_heading_deg;
    map3(attitude_imu_to_body, g_imu.gyro_dps, body);
    for (i = 0; i < 3; ++i)
        s->rates_dps[i] = body[i] - g_out.gyro_bias_dps[i];
    s->last_dt_s = g_last_dt;
    s->max_dt_s = g_max_dt;
    s->warmup_elapsed_s = g_warm_elapsed;
    s->imu_samples = g_imu_n;
    s->mag_samples = g_mag_n;
    s->dropped_samples = g_drop_n;
    s->imu_errors = g_imu_errors;
    s->mag_errors = g_mag_errors;
    s->warmup_samples = g_warm_n;
}

int attitude_est_cached_imu(lsm6_sample_t *sample, unsigned *sequence)
{
    XTime now;
    XTime_GetTime(&now);
    if (!g_imu_n || age(now, g_imu_tick, g_imu_n) > IMU_FRESH_S)
        return 0;
    if (sample) *sample = g_imu;
    if (sequence) *sequence = g_imu_n;
    return 1;
}

int attitude_est_cached_mag(mmc5603_sample_t *sample, unsigned *sequence)
{
    XTime now;
    XTime_GetTime(&now);
    if (!g_mag_n || age(now, g_mag_tick, g_mag_n) > MAG_FRESH_S)
        return 0;
    if (sample) *sample = g_mag;
    if (sequence) *sequence = g_mag_n;
    return 1;
}

static int deg10(float x)
{
    return (int)(x * 10.0f + (x >= 0.0f ? 0.5f : -0.5f));
}

float attitude_roll_rad(void) { return g_out.roll_deg * DEG_TO_RAD; }
float attitude_pitch_rad(void) { return g_out.pitch_deg * DEG_TO_RAD; }
float attitude_yaw_rad(void)
{
    float a = g_out.compass_heading_deg * DEG_TO_RAD;
    return a > PI_F ? a - 2.0f * PI_F : a;
}
int attitude_roll_deg10(void) { return deg10(g_out.roll_deg); }
int attitude_pitch_deg10(void) { return deg10(g_out.pitch_deg); }
int attitude_yaw_deg(void) { return ((int)(g_out.compass_heading_deg + 0.5f)) % 360; }
void attitude_rates_dps(float *rr, float *pr, float *yr)
{
    attitude_est_snapshot_t s;
    attitude_est_get_snapshot(&s);
    if (rr) *rr = s.rates_dps[0];
    if (pr) *pr = s.rates_dps[1];
    if (yr) *yr = s.rates_dps[2];
}
int attitude_imu_ok(void)
{
    attitude_est_snapshot_t s;
    attitude_est_get_snapshot(&s);
    return s.ready;
}
int attitude_mag_ok(void)
{
    attitude_est_snapshot_t s;
    attitude_est_get_snapshot(&s);
    return s.heading_valid;
}
