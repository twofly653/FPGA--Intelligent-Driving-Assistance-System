#include "compass_model.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define MIN_SPAN_MG 120
#define MIN_SAMPLES 40
#define MIN_SWEEP_RAD (330.0f * PI_F / 180.0f)
#define MAX_STEP_RAD (120.0f * PI_F / 180.0f)
#define MAX_FIT_RMS 0.07f
#define MAX_FIT_POINT_ERROR 0.25f

static int corrected_field(const compass_model_t *m, int x, int y,
                            float *angle, float *radius)
{
    float dx = (float)x - m->offset_x, dy = (float)y - m->offset_y;
    float nx = m->correction_xx * dx + m->correction_xy * dy;
    float ny = m->correction_xy * dx + m->correction_yy * dy;
    float r = sqrtf(nx * nx + ny * ny);
    if (!isfinite(r) || r < 0.65f || r > 1.35f) return 0;
    *angle = atan2f(ny, nx);
    if (radius) *radius = r;
    return 1;
}

static int history_index(const compass_model_t *m, int order)
{
    int oldest = m->write_index - m->stored;
    if (oldest < 0) oldest += COMPASS_CAL_MAX_SAMPLES;
    return (oldest + order) % COMPASS_CAL_MAX_SAMPLES;
}

static void recompute_extrema(compass_model_t *m)
{
    int i;
    m->min_x = m->min_y = 2147483647;
    m->max_x = m->max_y = -2147483647;
    for (i = 0; i < m->stored; ++i) {
        const compass_cal_sample_t *s = &m->history[i];
        if (s->x_mg < m->min_x) m->min_x = s->x_mg;
        if (s->x_mg > m->max_x) m->max_x = s->x_mg;
        if (s->y_mg < m->min_y) m->min_y = s->y_mg;
        if (s->y_mg > m->max_y) m->max_y = s->y_mg;
    }
}

static float candidate_angle(const compass_model_t *m, int x, int y)
{
    float ox = ((float)m->min_x + (float)m->max_x)*0.5f;
    float oy = ((float)m->min_y + (float)m->max_y)*0.5f;
    float sx = ((float)m->max_x - (float)m->min_x)*0.5f;
    float sy = ((float)m->max_y - (float)m->min_y)*0.5f;
    return atan2f(((float)y - oy)/sy, ((float)x - ox)/sx);
}

void compass_model_init(compass_model_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    /* Provisional chirality only. A one-point reference cannot establish it.
     * Compare a second heading (ideally all four) and FLIP if it runs backward. */
    m->direction = -1;
}

void compass_model_begin(compass_model_t *m)
{
    if (!m) return;
    m->min_x = m->min_y = 2147483647;
    m->max_x = m->max_y = -2147483647;
    m->samples = m->stored = m->write_index = 0;
    m->collecting = 1;
    /* Keep the committed calibration until the replacement passes all gates. */
}

void compass_model_cancel(compass_model_t *m)
{
    if (m) m->collecting = 0;
}

void compass_model_offer(compass_model_t *m, int x_mg, int y_mg)
{
    if (!m || !m->collecting) return;
    m->history[m->write_index].x_mg = x_mg;
    m->history[m->write_index].y_mg = y_mg;
    m->write_index = (m->write_index + 1)%COMPASS_CAL_MAX_SAMPLES;
    if (m->stored < COMPASS_CAL_MAX_SAMPLES) ++m->stored;
    if (x_mg < m->min_x) m->min_x = x_mg;
    if (x_mg > m->max_x) m->max_x = x_mg;
    if (y_mg < m->min_y) m->min_y = y_mg;
    if (y_mg > m->max_y) m->max_y = y_mg;
    if (m->samples < 1000000) ++m->samples;
    if (m->samples > COMPASS_CAL_MAX_SAMPLES) recompute_extrema(m);
}

int compass_model_span_x(const compass_model_t *m)
{
    return m && m->samples ? m->max_x - m->min_x : 0;
}

int compass_model_span_y(const compass_model_t *m)
{
    return m && m->samples ? m->max_y - m->min_y : 0;
}

int compass_model_coverage(const compass_model_t *m)
{
    unsigned mask = 0;
    int i, sectors = 0;
    if (!m || !m->stored || compass_model_span_x(m) < 20 ||
        compass_model_span_y(m) < 20) return 0;
    for (i = 0; i < m->stored; ++i) {
        const compass_cal_sample_t *s = &m->history[i];
        float a = candidate_angle(m, s->x_mg, s->y_mg);
        int sector = (int)((a + PI_F)*(8.0f/(2.0f*PI_F)));
        if (sector > 7) sector = 7;
        if (sector < 0) sector = 0;
        mask |= 1u << sector;
    }
    for (i = 0; i < 8; ++i) if (mask & (1u << i)) ++sectors;
    return sectors;
}

/* Small pivoted solve. Normalize before normal equations; reject singular fits. */
static int solve5(double a[5][6], double answer[5])
{
    int col, row, k;
    for (col = 0; col < 5; ++col) {
        int pivot = col;
        double scale;
        for (row = col + 1; row < 5; ++row)
            if (fabs(a[row][col]) > fabs(a[pivot][col])) pivot = row;
        if (fabs(a[pivot][col]) < 1e-8) return 0;
        if (pivot != col) for (k = col; k < 6; ++k) {
            double tmp = a[pivot][k];
            a[pivot][k] = a[col][k]; a[col][k] = tmp;
        }
        scale = a[col][col];
        for (k = col; k < 6; ++k) a[col][k] /= scale;
        for (row = 0; row < 5; ++row) if (row != col) {
            double f = a[row][col];
            for (k = col; k < 6; ++k) a[row][k] -= f*a[col][k];
        }
    }
    for (col = 0; col < 5; ++col) {
        answer[col] = a[col][5];
        if (!isfinite(answer[col])) return 0;
    }
    return 1;
}

typedef struct { float ox, oy, xx, xy, yy, fit; } ellipse_fit_t;

static int fit_ellipse(const compass_model_t *m, ellipse_fit_t *fit)
{
    double matrix[5][6] = {{0}}, p[5];
    double ox = ((double)m->min_x + (double)m->max_x)*0.5;
    double oy = ((double)m->min_y + (double)m->max_y)*0.5;
    double scale = fmax((double)compass_model_span_x(m),
                        (double)compass_model_span_y(m))*0.5;
    double a, b, c, det, cx, cy, k, qxx, qxy, qyy, rootdet, denom;
    int i, row, col;
    if (scale < 1.0) return 0;
    for (i = 0; i < m->stored; ++i) {
        const compass_cal_sample_t *s = &m->history[i];
        double x = ((double)s->x_mg - ox)/scale;
        double y = ((double)s->y_mg - oy)/scale;
        double v[5] = {x*x, x*y, y*y, x, y};
        for (row = 0; row < 5; ++row) {
            for (col = 0; col < 5; ++col) matrix[row][col] += v[row]*v[col];
            matrix[row][5] += v[row];
        }
    }
    if (!solve5(matrix, p)) return 0;
    a = p[0]; b = p[1]*0.5; c = p[2]; det = a*c - b*b;
    if (a <= 0.0 || c <= 0.0 || det <= 1e-8) return 0;
    cx = -(c*p[3] - b*p[4])/(2.0*det);
    cy = -(a*p[4] - b*p[3])/(2.0*det);
    if (fabs(cx) > 0.75 || fabs(cy) > 0.75) return 0;
    k = 1.0 + a*cx*cx + 2.0*b*cx*cy + c*cy*cy;
    if (k <= 0.0) return 0;
    qxx = a/k; qxy = b/k; qyy = c/k;
    det = qxx*qyy - qxy*qxy;
    /* Condition bound: semiaxis ratio at most about 8:1. */
    if (det <= 0.0 || (qxx+qyy)*(qxx+qyy)/det > 66.0) return 0;
    rootdet = sqrt(det);
    denom = sqrt(qxx + qyy + 2.0*rootdet)*scale;
    if (!isfinite(denom) || denom <= 0.0) return 0;
    /* Symmetric positive square root maps the fitted ellipse into a circle. */
    fit->ox = (float)(ox + scale*cx); fit->oy = (float)(oy + scale*cy);
    fit->xx = (float)((qxx + rootdet)/denom);
    fit->xy = (float)(qxy/denom);
    fit->yy = (float)((qyy + rootdet)/denom);
    fit->fit = 0.0f;
    return 1;
}

static float fit_angle(const ellipse_fit_t *f, int x, int y, float *error)
{
    float dx = (float)x - f->ox, dy = (float)y - f->oy;
    float nx = f->xx*dx + f->xy*dy, ny = f->xy*dx + f->yy*dy;
    *error = sqrtf(nx*nx + ny*ny) - 1.0f;
    return atan2f(ny, nx);
}

int compass_model_finish(compass_model_t *m)
{
    ellipse_fit_t fit;
    unsigned mask = 0;
    int i;
    float previous = 0.0f, sweep = 0.0f, errors = 0.0f;
    if (!m || !m->collecting) return 0;
    if (m->stored < MIN_SAMPLES || compass_model_span_x(m) < MIN_SPAN_MG ||
        compass_model_span_y(m) < MIN_SPAN_MG ||
        compass_model_coverage(m) < 8 || !fit_ellipse(m, &fit)) return 0;
    for (i = 0; i < m->stored; ++i) {
        const compass_cal_sample_t *s = &m->history[history_index(m, i)];
        float error, a = fit_angle(&fit, s->x_mg, s->y_mg, &error);
        int sector = (int)((a + PI_F)*(8.0f/(2.0f*PI_F)));
        if (!isfinite(error) || fabsf(error) > MAX_FIT_POINT_ERROR) return 0;
        errors += error*error;
        if (sector > 7) sector = 7;
        if (sector < 0) sector = 0;
        mask |= 1u << sector;
        if (i > 0) {
            float step = a - previous;
            while (step > PI_F) step -= 2.0f*PI_F;
            while (step < -PI_F) step += 2.0f*PI_F;
            if (fabsf(step) > MAX_STEP_RAD) return 0;
            sweep += step;
        }
        previous = a;
    }
    fit.fit = sqrtf(errors/(float)m->stored);
    if (mask != 255u || fit.fit > MAX_FIT_RMS ||
        fabsf(sweep) < MIN_SWEEP_RAD) return 0;
    m->offset_x = fit.ox; m->offset_y = fit.oy;
    m->span_x = (float)compass_model_span_x(m)*0.5f;
    m->span_y = (float)compass_model_span_y(m)*0.5f;
    m->correction_xx = fit.xx; m->correction_xy = fit.xy;
    m->correction_yy = fit.yy; m->fit_error = fit.fit;
    m->collecting = 0; m->calibrated = 1; m->aligned = 0;
    m->direction_verified = 0;
    return 1;
}

int compass_model_align_reference(compass_model_t *m, int x_mg, int y_mg,
                                  int reference_deg10)
{
    float angle;
    if (!m || !m->calibrated || m->collecting || reference_deg10 < 0 ||
        reference_deg10 >= 3600 || !corrected_field(m, x_mg, y_mg, &angle, 0))
        return 0;
    m->north_zero = angle; m->reference_deg10 = reference_deg10; m->aligned = 1;
    return 1;
}

int compass_model_align(compass_model_t *m, int x_mg, int y_mg)
{
    return compass_model_align_reference(m, x_mg, y_mg, 0);
}

void compass_model_flip(compass_model_t *m)
{
    if (m) { m->direction = -m->direction; m->direction_verified = 0; }
}

void compass_model_confirm_direction(compass_model_t *m)
{
    if (m && m->calibrated && m->aligned && !m->collecting)
        m->direction_verified = 1;
}

int compass_model_heading_deg10(const compass_model_t *m, int x_mg, int y_mg,
                                int *degrees10)
{
    float angle, h;
    if (!m || !degrees10 || !m->calibrated || !m->aligned ||
        !corrected_field(m, x_mg, y_mg, &angle, 0)) return 0;
    h = (float)m->direction*(angle - m->north_zero)*(1800.0f/PI_F) +
        (float)m->reference_deg10;
    while (h < 0.0f) h += 3600.0f;
    while (h >= 3600.0f) h -= 3600.0f;
    *degrees10 = (int)(h + 0.5f)%3600;
    return 1;
}

int compass_model_heading(const compass_model_t *m, int x_mg, int y_mg,
                          int *degrees)
{
    int d10;
    if (!degrees || !compass_model_heading_deg10(m, x_mg, y_mg, &d10)) return 0;
    *degrees = ((d10 + 5)/10)%360;
    return 1;
}

int compass_model_preview_angle(int x_mg, int y_mg, int *degrees)
{
    float a;
    if (!degrees || (x_mg == 0 && y_mg == 0)) return 0;
    a = atan2f((float)y_mg, (float)x_mg)*(180.0f/PI_F);
    if (a < 0.0f) a += 360.0f;
    *degrees = (int)(a + 0.5f)%360;
    return 1;
}
