#include "attitude_ekf.h"

#include <math.h>
#include <string.h>

/* Algorithm derived from the error-state formulation in Joan Sola,
 * "Quaternion kinematics for the error-state Kalman filter", 2017,
 * https://arxiv.org/abs/1711.02508 . This implementation is original C code.
 * All covariance calculations use float because the target is Cortex-A9 VFP.
 */

#define EKF_N 6
#define PI_F 3.14159265358979323846f
#define DEG_TO_RAD (PI_F / 180.0f)
#define RAD_TO_DEG (180.0f / PI_F)
#define AGE_LIMIT_S 1000000.0f

static bool finite3(const float v[3]) {
    return v != NULL && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static float norm3(const float v[3]) {
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static bool unit3(const float in[3], float out[3], float *length) {
    if (!finite3(in)) return false;
    const float n = norm3(in);
    if (!isfinite(n) || n < 1.0e-6f) return false;
    for (int i = 0; i < 3; ++i) out[i] = in[i] / n;
    if (length != NULL) *length = n;
    return true;
}

static void quat_multiply(const float a[4], const float b[4], float out[4]) {
    out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}

static bool quat_normalize(float q[4]) {
    const float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!isfinite(n) || n < 1.0e-6f) return false;
    for (int i = 0; i < 4; ++i) q[i] /= n;
    return true;
}

static void quat_exp(const float rotation_vector[3], float q[4]) {
    const float angle = norm3(rotation_vector);
    if (angle < 1.0e-6f) {
        q[0] = 1.0f;
        for (int i = 0; i < 3; ++i) q[i + 1] = 0.5f * rotation_vector[i];
    } else {
        const float scale = sinf(0.5f * angle) / angle;
        q[0] = cosf(0.5f * angle);
        for (int i = 0; i < 3; ++i) q[i + 1] = rotation_vector[i] * scale;
    }
}

static void rotation_matrix(const float q[4], float r[3][3]) {
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    r[0][0] = 1.0f - 2.0f * (y * y + z * z);
    r[0][1] = 2.0f * (x * y - w * z);
    r[0][2] = 2.0f * (x * z + w * y);
    r[1][0] = 2.0f * (x * y + w * z);
    r[1][1] = 1.0f - 2.0f * (x * x + z * z);
    r[1][2] = 2.0f * (y * z - w * x);
    r[2][0] = 2.0f * (x * z - w * y);
    r[2][1] = 2.0f * (y * z + w * x);
    r[2][2] = 1.0f - 2.0f * (x * x + y * y);
}

static bool quat_from_rotation(const float r[3][3], float q[4]) {
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f) {
        const float s = sqrtf(trace + 1.0f) * 2.0f;
        q[0] = 0.25f * s;
        q[1] = (r[2][1] - r[1][2]) / s;
        q[2] = (r[0][2] - r[2][0]) / s;
        q[3] = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = sqrtf(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q[0] = (r[2][1] - r[1][2]) / s;
        q[1] = 0.25f * s;
        q[2] = (r[0][1] + r[1][0]) / s;
        q[3] = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = sqrtf(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q[0] = (r[0][2] - r[2][0]) / s;
        q[1] = (r[0][1] + r[1][0]) / s;
        q[2] = 0.25f * s;
        q[3] = (r[1][2] + r[2][1]) / s;
    } else {
        const float s = sqrtf(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q[0] = (r[1][0] - r[0][1]) / s;
        q[1] = (r[0][2] + r[2][0]) / s;
        q[2] = (r[1][2] + r[2][1]) / s;
        q[3] = 0.25f * s;
    }
    return quat_normalize(q);
}

static float wrap_pi(float a) {
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static void symmetrize_covariance(float p[EKF_N][EKF_N]) {
    for (int i = 0; i < EKF_N; ++i) {
        p[i][i] = fmaxf(p[i][i], 1.0e-12f);
        for (int j = i + 1; j < EKF_N; ++j) {
            const float average = 0.5f * (p[i][j] + p[j][i]);
            p[i][j] = average;
            p[j][i] = average;
        }
    }
}

static bool config_valid(const attitude_ekf_config_t *c) {
    return c != NULL && isfinite(c->gyro_std_dps) && c->gyro_std_dps > 0.0f &&
           isfinite(c->gyro_limit_dps) && c->gyro_limit_dps > 0.0f &&
           isfinite(c->bias_walk_dps_sqrt_s) && c->bias_walk_dps_sqrt_s >= 0.0f &&
           isfinite(c->accel_direction_std) && c->accel_direction_std > 0.0f &&
           isfinite(c->mag_heading_std_deg) && c->mag_heading_std_deg > 0.0f &&
           isfinite(c->accel_norm_tolerance_g) && c->accel_norm_tolerance_g > 0.0f &&
           isfinite(c->accel_gate_deg) && c->accel_gate_deg > 0.0f &&
           isfinite(c->mag_norm_tolerance_ratio) && c->mag_norm_tolerance_ratio > 0.0f &&
           isfinite(c->mag_gate_deg) && c->mag_gate_deg > 0.0f &&
           isfinite(c->max_dt_s) && c->max_dt_s > 0.0f;
}

void attitude_ekf_default_config(attitude_ekf_config_t *config) {
    if (config == NULL) return;
    /* Starting values, not calibrated performance claims. Tune from logs. */
    config->gyro_std_dps = 0.30f;
    config->gyro_limit_dps = 245.0f; /* current LSM6DSM is configured for +/-250 dps */
    config->bias_walk_dps_sqrt_s = 0.01f;
    config->accel_direction_std = 0.03f;
    config->mag_heading_std_deg = 5.0f;
    config->accel_norm_tolerance_g = 0.15f;
    /* Keep the gravity innovation gate conservative for a moving vehicle:
     * 0.2 g horizontal acceleration appears as about 11.3 degrees tilt.
     * True motion should be tracked primarily by the gyro prediction.
     */
    config->accel_gate_deg = 6.0f;
    config->mag_norm_tolerance_ratio = 0.25f;
    config->mag_gate_deg = 30.0f;
    config->max_dt_s = 0.10f;
}

bool attitude_ekf_init(attitude_ekf_t *ekf,
                       const attitude_ekf_config_t *config,
                       const float accel_g[3],
                       const float mag_mg[3]) {
    if (ekf == NULL) return false;
    attitude_ekf_config_t defaults;
    attitude_ekf_default_config(&defaults);
    /* Copy before clearing ekf: callers may pass &ekf->config to restart. */
    const attitude_ekf_config_t effective = config == NULL ? defaults : *config;
    const attitude_ekf_config_t *chosen = &effective;
    if (!config_valid(chosen)) return false;

    float up[3], accel_norm;
    if (!unit3(accel_g, up, &accel_norm) ||
        fabsf(accel_norm - 1.0f) > chosen->accel_norm_tolerance_g) return false;

    float north[3] = {1.0f, 0.0f, 0.0f};
    float mag_unit[3], mag_norm = 0.0f;
    bool use_mag = mag_mg != NULL;
    if (use_mag) {
        if (!unit3(mag_mg, mag_unit, &mag_norm) || mag_norm < 1.0f)
            return false;
        const float vertical = dot3(mag_unit, up);
        for (int i = 0; i < 3; ++i) north[i] = mag_unit[i] - vertical * up[i];
        if (dot3(north, north) < 0.02f) return false;
        if (!unit3(north, north, NULL)) return false;
    }
    if (!use_mag) {
        north[0] = 1.0f;
        north[1] = 0.0f;
        north[2] = 0.0f;
        if (fabsf(dot3(north, up)) > 0.95f) {
            north[0] = 0.0f;
            north[1] = 1.0f;
        }
        const float vertical = dot3(north, up);
        for (int i = 0; i < 3; ++i) north[i] -= vertical * up[i];
        if (!unit3(north, north, NULL)) return false;
    }
    float west[3];
    cross3(up, north, west);
    if (!unit3(west, west, NULL)) return false;
    cross3(west, up, north);

    const float r[3][3] = {
        {north[0], north[1], north[2]},
        {west[0], west[1], west[2]},
        {up[0], up[1], up[2]},
    };
    float q[4];
    if (!quat_from_rotation(r, q)) return false;

    memset(ekf, 0, sizeof(*ekf));
    ekf->config = *chosen;
    memcpy(ekf->q, q, sizeof(q));
    ekf->magnetic_heading_aligned = use_mag;
    ekf->mag_field_ref_mg = use_mag ? mag_norm : 0.0f;
    ekf->seconds_since_mag = use_mag ? 0.0f : AGE_LIMIT_S;
    const float tilt_std = 3.0f * DEG_TO_RAD;
    const float yaw_std = (use_mag ? 8.0f : 90.0f) * DEG_TO_RAD;
    const float bias_std = 1.0f * DEG_TO_RAD;
    ekf->p[0][0] = tilt_std * tilt_std;
    ekf->p[1][1] = tilt_std * tilt_std;
    ekf->p[2][2] = yaw_std * yaw_std;
    for (int i = 3; i < EKF_N; ++i) ekf->p[i][i] = bias_std * bias_std;
    ekf->initialized = true;
    return true;
}

bool attitude_ekf_realign_stationary(attitude_ekf_t *ekf,
                                     const float averaged_accel_g[3],
                                     const float averaged_mag_mg[3]) {
    if (ekf == NULL || !ekf->initialized) return false;
    attitude_ekf_t restarted;
    if (!attitude_ekf_init(&restarted, &ekf->config,
                           averaged_accel_g, averaged_mag_mg)) return false;
    memcpy(restarted.gyro_bias_rad_s, ekf->gyro_bias_rad_s,
           sizeof(restarted.gyro_bias_rad_s));
    *ekf = restarted;
    return true;
}

void attitude_ekf_set_gyro_bias_dps(attitude_ekf_t *ekf,
                                    const float bias_dps[3]) {
    if (ekf == NULL || !ekf->initialized || !finite3(bias_dps)) return;
    for (int i = 0; i < 3; ++i) ekf->gyro_bias_rad_s[i] = bias_dps[i] * DEG_TO_RAD;
}

/* After a left/world error injection, reset the error coordinates with
 * G_theta = I + 0.5 [delta_theta]_x (Sola, equations 314-317).
 */
static void inject_error(attitude_ekf_t *ekf, const float dx[EKF_N]) {
    float correction[4], updated[4];
    quat_exp(dx, correction);
    quat_multiply(correction, ekf->q, updated);
    if (quat_normalize(updated)) memcpy(ekf->q, updated, sizeof(updated));
    for (int i = 0; i < 3; ++i) ekf->gyro_bias_rad_s[i] += dx[i + 3];

    float g[EKF_N][EKF_N] = {{0}};
    for (int i = 0; i < EKF_N; ++i) g[i][i] = 1.0f;
    g[0][1] = -0.5f * dx[2];
    g[0][2] =  0.5f * dx[1];
    g[1][0] =  0.5f * dx[2];
    g[1][2] = -0.5f * dx[0];
    g[2][0] = -0.5f * dx[1];
    g[2][1] =  0.5f * dx[0];

    float temp[EKF_N][EKF_N] = {{0}};
    float reset[EKF_N][EKF_N] = {{0}};
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int k = 0; k < EKF_N; ++k) temp[i][j] += g[i][k] * ekf->p[k][j];
        }
    }
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int k = 0; k < EKF_N; ++k) reset[i][j] += temp[i][k] * g[j][k];
        }
    }
    memcpy(ekf->p, reset, sizeof(reset));
    symmetrize_covariance(ekf->p);
}

static bool invert3(const float matrix[3][3], float inverse[3][3]) {
    float augmented[3][6] = {{0}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) augmented[i][j] = matrix[i][j];
        augmented[i][i + 3] = 1.0f;
    }
    for (int column = 0; column < 3; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row) {
            if (fabsf(augmented[row][column]) > fabsf(augmented[pivot][column])) pivot = row;
        }
        if (fabsf(augmented[pivot][column]) < 1.0e-12f) return false;
        if (pivot != column) {
            for (int j = 0; j < 6; ++j) {
                const float swap = augmented[pivot][j];
                augmented[pivot][j] = augmented[column][j];
                augmented[column][j] = swap;
            }
        }
        const float divisor = augmented[column][column];
        for (int j = 0; j < 6; ++j) augmented[column][j] /= divisor;
        for (int row = 0; row < 3; ++row) {
            if (row == column) continue;
            const float factor = augmented[row][column];
            for (int j = 0; j < 6; ++j) augmented[row][j] -= factor * augmented[column][j];
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) inverse[i][j] = augmented[i][j + 3];
    }
    return true;
}

/* Gravity observes tilt, but does not measure rotation about world up.
 * Use a constrained Kalman gain so finite linearisation/covariance cross terms
 * cannot turn a gravity innovation into a yaw observation. The same constraint
 * also protects gyro bias along the current gravity axis; a stationary
 * accelerometer cannot identify that component. This conservative constraint
 * deliberately forgoes indirect yaw-bias inference from changing tilt.
 * Magnetic observations below retain the full gain.
 *
 * Joseph covariance form MUST use the constrained gain as well as the mean
 * update. Zeroing only dx would leave a falsely confident yaw covariance.
 */
static bool update_gravity_vector3(attitude_ekf_t *ekf,
                           const float h[3][EKF_N],
                           const float residual[3],
                           float variance, const float gravity_body[3]) {
    float pht[EKF_N][3] = {{0}};
    float innovation[3][3] = {{0}};
    for (int i = 0; i < EKF_N; ++i) {
        for (int a = 0; a < 3; ++a) {
            for (int j = 0; j < EKF_N; ++j) pht[i][a] += ekf->p[i][j] * h[a][j];
        }
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 3; ++b) {
            for (int i = 0; i < EKF_N; ++i) innovation[a][b] += h[a][i] * pht[i][b];
            if (a == b) innovation[a][b] += variance;
        }
    }
    float inverse[3][3];
    if (!invert3(innovation, inverse)) return false;

    float k_gain[EKF_N][3] = {{0}};
    float dx[EKF_N] = {0};
    float a_matrix[EKF_N][EKF_N] = {{0}};
    for (int i = 0; i < EKF_N; ++i) {
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) k_gain[i][a] += pht[i][b] * inverse[b][a];
        }
    }
    /* Error coordinates are [world angle, body gyro bias]. Apply
     * diag(I-up_world*up_world^T, I-gravity_body*gravity_body^T) to K.
     * gravity_body = R^T*up_world is unit length for the normalised q.
     */
    for (int a = 0; a < 3; ++a) {
        k_gain[2][a] = 0.0f;
        float along_gravity = 0.0f;
        for (int i = 0; i < 3; ++i)
            along_gravity += gravity_body[i] * k_gain[i + 3][a];
        for (int i = 0; i < 3; ++i)
            k_gain[i + 3][a] -= gravity_body[i] * along_gravity;
    }
    for (int i = 0; i < EKF_N; ++i) {
        for (int a = 0; a < 3; ++a) dx[i] += k_gain[i][a] * residual[a];
        for (int j = 0; j < EKF_N; ++j) {
            a_matrix[i][j] = i == j ? 1.0f : 0.0f;
            for (int a = 0; a < 3; ++a) a_matrix[i][j] -= k_gain[i][a] * h[a][j];
        }
    }

    float temp[EKF_N][EKF_N] = {{0}};
    float updated[EKF_N][EKF_N] = {{0}};
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int t = 0; t < EKF_N; ++t) temp[i][j] += a_matrix[i][t] * ekf->p[t][j];
        }
    }
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int t = 0; t < EKF_N; ++t) updated[i][j] += temp[i][t] * a_matrix[j][t];
            for (int a = 0; a < 3; ++a) updated[i][j] += variance * k_gain[i][a] * k_gain[j][a];
        }
    }
    memcpy(ekf->p, updated, sizeof(updated));
    inject_error(ekf, dx);
    return true;
}

static bool update_scalar(attitude_ekf_t *ekf,
                          const float h[EKF_N],
                          float residual,
                          float variance) {
    float ph[EKF_N] = {0};
    float innovation = variance;
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) ph[i] += ekf->p[i][j] * h[j];
        innovation += h[i] * ph[i];
    }
    if (!isfinite(innovation) || innovation <= 1.0e-12f) return false;

    float k_gain[EKF_N], dx[EKF_N];
    float a_matrix[EKF_N][EKF_N];
    for (int i = 0; i < EKF_N; ++i) {
        k_gain[i] = ph[i] / innovation;
        dx[i] = k_gain[i] * residual;
        for (int j = 0; j < EKF_N; ++j)
            a_matrix[i][j] = (i == j ? 1.0f : 0.0f) - k_gain[i] * h[j];
    }
    float temp[EKF_N][EKF_N] = {{0}};
    float updated[EKF_N][EKF_N] = {{0}};
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int t = 0; t < EKF_N; ++t) temp[i][j] += a_matrix[i][t] * ekf->p[t][j];
        }
    }
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int t = 0; t < EKF_N; ++t) updated[i][j] += temp[i][t] * a_matrix[j][t];
            updated[i][j] += variance * k_gain[i] * k_gain[j];
        }
    }
    memcpy(ekf->p, updated, sizeof(updated));
    inject_error(ekf, dx);
    return true;
}

static bool predict(attitude_ekf_t *ekf, const float gyro_dps[3], float dt_s) {
    float omega[3], half_rotation[3], rotation[3];
    for (int i = 0; i < 3; ++i) {
        omega[i] = gyro_dps[i] * DEG_TO_RAD - ekf->gyro_bias_rad_s[i];
        rotation[i] = omega[i] * dt_s;
        half_rotation[i] = 0.5f * rotation[i];
    }
    float half_q[4], middle_q[4], delta_q[4], next_q[4];
    quat_exp(half_rotation, half_q);
    quat_multiply(ekf->q, half_q, middle_q);
    if (!quat_normalize(middle_q)) return false;
    quat_exp(rotation, delta_q);
    quat_multiply(ekf->q, delta_q, next_q);
    if (!quat_normalize(next_q)) return false;

    float r_middle[3][3];
    rotation_matrix(middle_q, r_middle);
    float transition[EKF_N][EKF_N] = {{0}};
    for (int i = 0; i < EKF_N; ++i) transition[i][i] = 1.0f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) transition[i][j + 3] = -r_middle[i][j] * dt_s;
    }
    float temp[EKF_N][EKF_N] = {{0}};
    float predicted[EKF_N][EKF_N] = {{0}};
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int t = 0; t < EKF_N; ++t) temp[i][j] += transition[i][t] * ekf->p[t][j];
        }
    }
    for (int i = 0; i < EKF_N; ++i) {
        for (int j = 0; j < EKF_N; ++j) {
            for (int t = 0; t < EKF_N; ++t) predicted[i][j] += temp[i][t] * transition[j][t];
        }
    }
    /* gyro_std_dps is per-sample rate noise: angle variance uses dt^2.
     * bias_walk_dps_sqrt_s is a continuous random walk: bias variance uses dt.
     */
    const float gyro_angle_std = ekf->config.gyro_std_dps * DEG_TO_RAD * dt_s;
    const float bias_walk_std = ekf->config.bias_walk_dps_sqrt_s * DEG_TO_RAD;
    for (int i = 0; i < 3; ++i) {
        predicted[i][i] += gyro_angle_std * gyro_angle_std;
        predicted[i + 3][i + 3] += bias_walk_std * bias_walk_std * dt_s;
    }
    memcpy(ekf->p, predicted, sizeof(predicted));
    symmetrize_covariance(ekf->p);
    memcpy(ekf->q, next_q, sizeof(next_q));
    return true;
}

static bool update_accelerometer(attitude_ekf_t *ekf, const float accel_g[3]) {
    float measured[3], measured_norm;
    if (!unit3(accel_g, measured, &measured_norm)) return false;
    const float norm_error = fabsf(measured_norm - 1.0f);
    if (norm_error > ekf->config.accel_norm_tolerance_g) return false;

    float r[3][3];
    rotation_matrix(ekf->q, r);
    const float expected[3] = {r[2][0], r[2][1], r[2][2]};
    const float innovation_angle = acosf(clampf(dot3(measured, expected), -1.0f, 1.0f));
    const float gate = ekf->config.accel_gate_deg * DEG_TO_RAD;
    if (innovation_angle > gate) return false;

    /* h = R^T * up; H = R^T * [up]_x. The 3-D vector has only two
     * independent directions, but positive measurement variance makes S
     * invertible. Inflate R near dynamic-motion boundaries.
     */
    const float skew_up[3][3] = {
        {0.0f, -1.0f, 0.0f},
        {1.0f,  0.0f, 0.0f},
        {0.0f,  0.0f, 0.0f},
    };
    float h[3][EKF_N] = {{0}};
    float residual[3];
    for (int a = 0; a < 3; ++a) {
        residual[a] = measured[a] - expected[a];
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) h[a][j] += r[k][a] * skew_up[k][j];
        }
    }
    const float angle_fraction = innovation_angle / gate;
    const float norm_fraction = norm_error / ekf->config.accel_norm_tolerance_g;
    const float sigma = ekf->config.accel_direction_std *
                        (1.0f + 8.0f * angle_fraction * angle_fraction +
                         4.0f * norm_fraction * norm_fraction);
    return update_gravity_vector3(ekf, h, residual, sigma * sigma, expected);
}

static bool update_magnetometer(attitude_ekf_t *ekf, const float mag_mg[3]) {
    if (!ekf->magnetic_heading_aligned) return false;
    float measured[3], measured_norm;
    if (!unit3(mag_mg, measured, &measured_norm)) return false;
    if (measured_norm < 1.0f) return false;

    const float reference = ekf->mag_field_ref_mg;
    const float norm_fraction = reference > 0.0f ?
        fabsf(measured_norm - reference) / reference : 0.0f;
    if (reference > 0.0f && norm_fraction > ekf->config.mag_norm_tolerance_ratio)
        return false;

    float r[3][3], measured_world[3] = {0};
    rotation_matrix(ekf->q, r);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) measured_world[i] += r[i][j] * measured[j];
    }
    const float horizontal_sq = measured_world[0] * measured_world[0] +
                                measured_world[1] * measured_world[1];
    if (horizontal_sq < 0.02f) return false; /* little heading information */

    const float angle = wrap_pi(atan2f(measured_world[1], measured_world[0]));
    const float gate = ekf->config.mag_gate_deg * DEG_TO_RAD;
    if (fabsf(angle) > gate) return false;

    /* z=0, h(q)=atan2((R*m)_y,(R*m)_x). Exact first derivative under a
     * left/world angular perturbation, including magnetic inclination.
     * This is a heading-only observation; magnetic vertical magnitude is
     * deliberately not used as a separate tilt measurement.
     */
    const float h[EKF_N] = {
        -measured_world[0] * measured_world[2] / horizontal_sq,
        -measured_world[1] * measured_world[2] / horizontal_sq,
        1.0f, 0.0f, 0.0f, 0.0f,
    };
    const float angle_fraction = fabsf(angle) / gate;
    const float field_fraction = norm_fraction / ekf->config.mag_norm_tolerance_ratio;
    const float sigma = ekf->config.mag_heading_std_deg * DEG_TO_RAD *
                        (1.0f + 3.0f * angle_fraction * angle_fraction +
                         2.0f * field_fraction * field_fraction);
    return update_scalar(ekf, h, -angle, sigma * sigma);
}

bool attitude_ekf_step(attitude_ekf_t *ekf,
                       const float gyro_dps[3],
                       const float accel_g[3],
                       const float mag_mg[3],
                       float dt_s) {
    if (ekf == NULL || !ekf->initialized) return false;
    bool gyro_in_range = finite3(gyro_dps);
    if (gyro_in_range) {
        for (int i = 0; i < 3; ++i) {
            if (fabsf(gyro_dps[i]) >= ekf->config.gyro_limit_dps)
                gyro_in_range = false;
        }
    }
    if (!gyro_in_range || !isfinite(dt_s) || dt_s <= 0.0f ||
        dt_s > ekf->config.max_dt_s) {
        ekf->accel_used_this_step = false;
        ekf->mag_used_this_step = false;
        if (isfinite(dt_s) && dt_s > 0.0f) {
            ekf->seconds_since_accel = fminf(AGE_LIMIT_S, ekf->seconds_since_accel + dt_s);
            ekf->seconds_since_mag = fminf(AGE_LIMIT_S, ekf->seconds_since_mag + dt_s);
        } else {
            ekf->seconds_since_accel = AGE_LIMIT_S;
            ekf->seconds_since_mag = AGE_LIMIT_S;
        }
        ++ekf->skipped_dt;
        return false;
    }

    ekf->accel_used_this_step = false;
    ekf->mag_used_this_step = false;
    ekf->seconds_since_accel = fminf(AGE_LIMIT_S, ekf->seconds_since_accel + dt_s);
    ekf->seconds_since_mag = fminf(AGE_LIMIT_S, ekf->seconds_since_mag + dt_s);
    if (!predict(ekf, gyro_dps, dt_s)) {
        ++ekf->skipped_dt;
        return false;
    }

    if (accel_g != NULL) {
        if (update_accelerometer(ekf, accel_g)) {
            ekf->accel_used_this_step = true;
            ekf->seconds_since_accel = 0.0f;
            ++ekf->accel_accepted;
        } else {
            ++ekf->accel_rejected;
        }
    }
    if (mag_mg != NULL) {
        if (update_magnetometer(ekf, mag_mg)) {
            ekf->mag_used_this_step = true;
            ekf->seconds_since_mag = 0.0f;
            ++ekf->mag_accepted;
        } else {
            ++ekf->mag_rejected;
        }
    }
    return true;
}

bool attitude_ekf_get_output(const attitude_ekf_t *ekf,
                             attitude_ekf_output_t *output) {
    if (ekf == NULL || !ekf->initialized || output == NULL) return false;
    const float w = ekf->q[0], x = ekf->q[1], y = ekf->q[2], z = ekf->q[3];
    const float roll = atan2f(2.0f * (w * x + y * z),
                              1.0f - 2.0f * (x * x + y * y));
    const float pitch = asinf(clampf(2.0f * (w * y - z * x), -1.0f, 1.0f));
    const float yaw = atan2f(2.0f * (w * z + x * y),
                             1.0f - 2.0f * (y * y + z * z));
    output->roll_deg = roll * RAD_TO_DEG;
    output->pitch_deg = pitch * RAD_TO_DEG;
    output->yaw_deg = yaw * RAD_TO_DEG;
    output->compass_heading_deg = fmodf(360.0f - output->yaw_deg, 360.0f);
    for (int i = 0; i < 3; ++i)
        output->gyro_bias_dps[i] = ekf->gyro_bias_rad_s[i] * RAD_TO_DEG;
    output->yaw_model_sigma_deg = sqrtf(fmaxf(ekf->p[2][2], 0.0f)) * RAD_TO_DEG;
    output->seconds_since_mag = ekf->seconds_since_mag;
    output->roll_pitch_recent = ekf->seconds_since_accel <= 0.25f;
    output->heading_recent = ekf->magnetic_heading_aligned &&
                             ekf->seconds_since_mag <= 0.50f;
    output->accel_used_this_step = ekf->accel_used_this_step;
    output->mag_used_this_step = ekf->mag_used_this_step;
    return true;
}
