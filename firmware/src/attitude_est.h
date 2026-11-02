#ifndef ATTITUDE_EST_H
#define ATTITUDE_EST_H

#include "lsm6dsm.h"
#include "mmc5603.h"

typedef struct {
    int imu_detected, mag_detected;
    int ready, imu_fresh, mag_fresh, roll_pitch_recent;
    int heading_valid, mag_fusion_enabled, aligning;
    float roll_deg, pitch_deg;
    float yaw_deg;                 /* NWU yaw; relative if heading_valid == 0 */
    float heading_deg;             /* clockwise 0..360; relative without mag */
    float rates_dps[3];            /* body gyro rates, not Euler derivatives */
    float imu_age_s, mag_age_s;
    float last_dt_s, max_dt_s, warmup_elapsed_s;
    unsigned imu_samples, mag_samples, dropped_samples;
    unsigned imu_errors, mag_errors, warmup_samples;
} attitude_est_snapshot_t;

/* Call after i2c_init. Poll frequently; only new LSM6 samples are fused.
 * Initial alignment requires >=1 s of continuous still samples. This is a
 * sensor plausibility gate: the operator must actually keep the board still.
 */
void attitude_est_init(void);
void attitude_est_poll(void);
void attitude_est_get_snapshot(attitude_est_snapshot_t *snapshot);

/* Explicit operator request. Keep board still until ready; resets attitude
 * and gyro bias from a NEW stationary window. Overlong sample gaps require
 * this action, because missed rotation cannot be reconstructed without FIFO.
 */
void attitude_est_request_realign(void);

/* Raw chip-frame caches, untouched by attitude calibration/axis mapping.
 * Return 1 only while fresh (IMU <=250 ms, mag <=500 ms); else return 0.
 * sequence increments only on a successful NEW measurement; it may be NULL.
 * UI/telemetry must consume these caches rather than re-triggering sensors.
 */
int attitude_est_cached_imu(lsm6_sample_t *sample, unsigned *sequence);
int attitude_est_cached_mag(mmc5603_sample_t *sample, unsigned *sequence);

/* Compatibility getters. Yaw is clockwise relative heading in default mode.
 * mag_ok means trusted recent magnetic heading, not merely I2C presence.
 */
void attitude_rates_dps(float *roll_dps, float *pitch_dps, float *yaw_dps);

float attitude_roll_rad(void);
float attitude_pitch_rad(void);
float attitude_yaw_rad(void);

int attitude_roll_deg10(void);   /* 0.1 deg */
int attitude_pitch_deg10(void);
int attitude_yaw_deg(void);      /* 0..359 */

int attitude_imu_ok(void);
int attitude_mag_ok(void);

#endif
