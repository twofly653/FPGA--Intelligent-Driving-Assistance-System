#include "compass_service.h"
#include "attitude_est.h"
#include <math.h>
#include <string.h>

#define DEG_TO_RAD 0.01745329251994329577f
#define RAD_TO_DEG 57.29577951308232088f
#define CAL_MIN_STEP_MG 4.0f
#define CAL_AUTO_FIT_INTERVAL 8

static compass_model_t model;
static compass_service_snapshot_t state;
static mmc5603_sample_t raw;
static unsigned last_sequence;
static int have_sequence;
static double cal_z_sum, cal_z_square;
static unsigned cal_z_samples;
static int cal_z_history[COMPASS_CAL_MAX_SAMPLES];
static float calibrated_z, calibrated_z_tolerance;

static void metadata(int update_coverage)
{
    state.calibrating = model.collecting;
    state.calibrated = model.calibrated;
    state.aligned = model.aligned;
    state.span_x = compass_model_span_x(&model);
    state.span_y = compass_model_span_y(&model);
    if (update_coverage) state.coverage = compass_model_coverage(&model);
    state.cal_samples = model.samples;
    state.reference_deg10 = model.reference_deg10;
    state.direction = model.direction;
    state.direction_verified = model.direction_verified;
    state.fit_error = model.fit_error;
}

static void update_heading(void)
{
    int d10 = 0;
    float dx = (float)raw.x_mg - model.offset_x;
    float dy = (float)raw.y_mg - model.offset_y;
    float nx = model.correction_xx*dx + model.correction_xy*dy;
    float ny = model.correction_xy*dx + model.correction_yy*dy;
    float radius = sqrtf(nx*nx + ny*ny);
    state.field_ok = model.calibrated && isfinite(radius) &&
        radius >= 0.65f && radius <= 1.35f &&
        fabsf((float)raw.z_mg - calibrated_z) <= calibrated_z_tolerance;
    state.valid = state.detected && state.fresh && state.level &&
        state.acceleration_ok && state.field_ok && !model.collecting &&
        compass_model_heading_deg10(&model, raw.x_mg, raw.y_mg, &d10);
    /* Invalid values are explicitly cleared; consumers must honor valid. */
    state.heading_deg10 = state.valid ? d10 : 0;
    state.heading_deg = (float)state.heading_deg10*0.1f;
}

void compass_service_init(void)
{
    compass_model_init(&model);
    memset(&state, 0, sizeof(state));
    memset(&raw, 0, sizeof(raw));
    last_sequence = 0; have_sequence = 0;
    cal_z_sum = cal_z_square = 0.0; cal_z_samples = 0;
    calibrated_z = 0.0f; calibrated_z_tolerance = 0.0f;
    metadata(1);
}

void compass_service_poll(void)
{
    attitude_est_snapshot_t attitude;
    lsm6_sample_t imu;
    mmc5603_sample_t sample;
    unsigned sequence = last_sequence;
    int mag_cached, imu_cached, new_sample = 0, coverage_due = 0;
    float c, acc_norm = 0.0f;
    attitude_est_get_snapshot(&attitude);
    imu_cached = attitude_est_cached_imu(&imu, 0);
    if (imu_cached) acc_norm = sqrtf(imu.acc_g[0]*imu.acc_g[0] +
        imu.acc_g[1]*imu.acc_g[1] + imu.acc_g[2]*imu.acc_g[2]);
    state.acceleration_ok = imu_cached && isfinite(acc_norm) &&
        acc_norm >= 0.85f && acc_norm <= 1.15f;
    state.detected = attitude.mag_detected;
    state.tilt_deg = 180.0f;
    if (attitude.ready && attitude.imu_fresh && attitude.roll_pitch_recent &&
        isfinite(attitude.roll_deg) && isfinite(attitude.pitch_deg)) {
        c = cosf(attitude.roll_deg*DEG_TO_RAD)*
            cosf(attitude.pitch_deg*DEG_TO_RAD);
        if (c > 1.0f) c = 1.0f;
        if (c < -1.0f) c = -1.0f;
        state.tilt_deg = acosf(c)*RAD_TO_DEG;
    }
    state.level = state.tilt_deg <= COMPASS_LEVEL_LIMIT_DEG &&
        state.acceleration_ok;
    mag_cached = attitude_est_cached_mag(&sample, &sequence);
    state.fresh = mag_cached && attitude.mag_fresh;
    if (state.fresh) {
        raw = sample;
        new_sample = !have_sequence || sequence != last_sequence;
        have_sequence = 1;
        last_sequence = sequence;
        state.sequence = sequence;
        state.raw_x_mg = raw.x_mg; state.raw_y_mg = raw.y_mg;
        state.raw_z_mg = raw.z_mg;
    }
    if (new_sample && model.collecting) {
        if (state.level && state.detected) {
            int accept = !model.stored;
            if (model.stored) {
                int last = (model.write_index + COMPASS_CAL_MAX_SAMPLES - 1)%
                    COMPASS_CAL_MAX_SAMPLES;
                float dx = (float)raw.x_mg - (float)model.history[last].x_mg;
                float dy = (float)raw.y_mg - (float)model.history[last].y_mg;
                accept = dx*dx + dy*dy >= CAL_MIN_STEP_MG*CAL_MIN_STEP_MG;
            }
            if (accept) {
                int index = model.write_index;
                /* Z statistics must describe the SAME retained points as XY. */
                if (model.stored == COMPASS_CAL_MAX_SAMPLES) {
                    double old_z = (double)cal_z_history[index];
                    cal_z_sum -= old_z; cal_z_square -= old_z*old_z;
                } else ++cal_z_samples;
                compass_model_offer(&model, raw.x_mg, raw.y_mg);
                cal_z_history[index] = raw.z_mg;
                cal_z_sum += (double)raw.z_mg;
                cal_z_square += (double)raw.z_mg*(double)raw.z_mg;
                coverage_due = model.samples%CAL_AUTO_FIT_INTERVAL == 0;
                /* Lock the first trajectory passing ALL existing fit gates.
                 * Waiting for a human to press END must not erase a full turn
                 * with another 1024 stationary measurements. */
                if (model.stored >= 40 && coverage_due)
                    (void)compass_service_finish_calibration();
            }
        } else ++state.rejected_tilt_samples;
    }
    metadata(coverage_due);
    update_heading();
}

void compass_service_get_snapshot(compass_service_snapshot_t *snapshot)
{
    if (snapshot) *snapshot = state;
}

const compass_model_t *compass_service_model(void) { return &model; }

void compass_service_begin_calibration(void)
{
    compass_model_begin(&model);
    cal_z_sum = cal_z_square = 0.0; cal_z_samples = 0;
    state.rejected_tilt_samples = 0;
    metadata(1); update_heading();
}

int compass_service_finish_calibration(void)
{
    double variance, zmean;
    if (!model.collecting) return model.calibrated;
    if (cal_z_samples < 40) return 0;
    zmean = cal_z_sum/(double)cal_z_samples;
    variance = cal_z_square/(double)cal_z_samples - zmean*zmean;
    if (variance < 0.0) variance = 0.0;
    /* A planar turn should leave the sensor Z field nearly constant. */
    if (sqrt(variance) > 80.0) return 0;
    if (!compass_model_finish(&model)) { metadata(1); return 0; }
    calibrated_z = (float)zmean;
    calibrated_z_tolerance = fmaxf(100.0f, 4.0f*(float)sqrt(variance));
    metadata(1); update_heading();
    return 1;
}

void compass_service_cancel_calibration(void)
{
    compass_model_cancel(&model); metadata(1); update_heading();
}

int compass_service_align_reference(int reference_deg10)
{
    int ok;
    compass_service_poll();
    if (!state.fresh || !state.detected || !state.level || !state.field_ok)
        return 0;
    ok = compass_model_align_reference(&model, raw.x_mg, raw.y_mg,
                                       reference_deg10);
    metadata(0); update_heading();
    return ok;
}

void compass_service_flip_direction(void)
{
    compass_model_flip(&model); metadata(0); update_heading();
}

void compass_service_confirm_direction(void)
{
    if (state.valid) compass_model_confirm_direction(&model);
    metadata(0);
}
