#ifndef ATTITUDE_EKF_H
#define ATTITUDE_EKF_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Six-error-state, nine-axis attitude EKF for a vehicle.
 * Body axes: x forward, y left, z up. Navigation axes: x magnetic north,
 * y west, z up (NWU). q rotates body vectors into the navigation frame.
 * Inputs must already be calibrated and remapped into the body axes.
 * Gyroscope: degrees/s; accelerometer: g; magnetometer: milligauss.
 * Supply mag_mg only for a NEW magnetic sample; pass NULL otherwise.
 * This module has no hardware or Xilinx dependencies.
 */

typedef struct {
    float gyro_std_dps;              /* sample-to-sample gyro standard deviation */
    float gyro_limit_dps;            /* reject saturated/out-of-range angular rates */
    float bias_walk_dps_sqrt_s;      /* gyro-bias random-walk spectral std. dev. */
    float accel_direction_std;       /* normalized acceleration component std. dev. */
    float mag_heading_std_deg;       /* magnetic-heading measurement std. dev. */
    float accel_norm_tolerance_g;    /* allowed |norm(accel)-1| before rejection */
    float accel_gate_deg;            /* gravity-direction innovation gate */
    float mag_norm_tolerance_ratio;  /* relative change from calibrated field norm */
    float mag_gate_deg;              /* magnetic-heading innovation gate */
    float max_dt_s;                  /* skip a sample when its timestamp gap exceeds this */
} attitude_ekf_config_t;

typedef struct {
    float q[4];                      /* w,x,y,z: body -> magnetic NWU */
    float gyro_bias_rad_s[3];
    float p[6][6];                   /* error [world-frame angle(rad), body gyro bias(rad/s)] */
    float mag_field_ref_mg;
    float seconds_since_accel;
    float seconds_since_mag;
    uint32_t accel_accepted;
    uint32_t accel_rejected;
    uint32_t mag_accepted;
    uint32_t mag_rejected;
    uint32_t skipped_dt;
    bool initialized;
    bool magnetic_heading_aligned;
    bool accel_used_this_step;
    bool mag_used_this_step;
    attitude_ekf_config_t config;
} attitude_ekf_t;

typedef struct {
    float roll_deg;
    float pitch_deg;
    float yaw_deg;                   /* positive turn toward west */
    float compass_heading_deg;       /* clockwise from magnetic north */
    float gyro_bias_dps[3];
    float yaw_model_sigma_deg;       /* model covariance only; excludes calibration error */
    float seconds_since_mag;
    bool roll_pitch_recent;          /* accepted accelerometer within 0.25 s */
    bool heading_recent;             /* accepted magnetometer within 0.5 s */
    bool accel_used_this_step;
    bool mag_used_this_step;
} attitude_ekf_output_t;

void attitude_ekf_default_config(attitude_ekf_config_t *config);

/* Initialise using an average of samples collected while stationary.
 * mag_mg must be calibrated, axis-aligned, and checked for local interference
 * by the caller. Pass NULL if unsure: yaw then starts at zero and remains
 * relative. A non-NULL invalid magnetic vector makes initialisation fail.
 * To establish magnetic north later, reinitialise from stationary
 * averaged accel/mag samples; one arbitrary late sample is not auto-accepted.
 */
bool attitude_ekf_init(attitude_ekf_t *ekf,
                       const attitude_ekf_config_t *config,
                       const float accel_g[3],
                       const float mag_mg[3]);

/* Recover from a large attitude error after the caller has independently
 * confirmed that the vehicle is stationary and averaged fresh sensor samples.
 * The normal 6-degree gravity gate cannot recover a larger tilt error on its
 * own. This restarts attitude, covariance, freshness, and counters while
 * retaining the current gyro-bias estimate and configuration. If it fails,
 * the previous filter remains untouched. mag_mg may be NULL if not trusted.
 */
bool attitude_ekf_realign_stationary(attitude_ekf_t *ekf,
                                     const float averaged_accel_g[3],
                                     const float averaged_mag_mg[3]);

/* Set a stationary gyro average before normal updates, if available. */
void attitude_ekf_set_gyro_bias_dps(attitude_ekf_t *ekf,
                                    const float bias_dps[3]);

/* One update per NEW IMU sample; dt_s comes from actual sample timestamps.
 * accel_g may be NULL after a failed accelerometer read. mag_mg is NULL on
 * IMU ticks without a new magnetometer sample. Returns false for invalid gyro
 * data, an uninitialised filter, or an invalid/overlong dt. On rejection the
 * attitude and covariance are unchanged, while freshness/diagnostic flags
 * are updated.
 */
bool attitude_ekf_step(attitude_ekf_t *ekf,
                       const float gyro_dps[3],
                       const float accel_g[3],
                       const float mag_mg[3],
                       float dt_s);

bool attitude_ekf_get_output(const attitude_ekf_t *ekf,
                             attitude_ekf_output_t *output);

#endif
