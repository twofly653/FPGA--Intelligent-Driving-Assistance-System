#ifndef COMPASS_MODEL_H
#define COMPASS_MODEL_H

/* Planar magnetic bearing. Full XY ellipse calibration corrects hard iron and
 * symmetric soft iron; this is NOT a three-axis / tilt-compensated compass.
 * An external magnetic-north reference and installation check are required. */
#define COMPASS_CAL_MAX_SAMPLES 1024

typedef struct {
    int x_mg, y_mg;
} compass_cal_sample_t;

typedef struct {
    int min_x, max_x, min_y, max_y;
    int samples;
    int collecting;
    int calibrated;
    int aligned;
    int direction;
    float offset_x, offset_y;
    float span_x, span_y;
    float north_zero;
    float correction_xx, correction_xy, correction_yy;
    float fit_error;             /* RMS fractional radial fit residual. */
    int reference_deg10;         /* Bearing when north_zero was measured. */
    int direction_verified;     /* Operator checked a SECOND direction. */
    int stored;
    int write_index;
    compass_cal_sample_t history[COMPASS_CAL_MAX_SAMPLES];
} compass_model_t;

void compass_model_init(compass_model_t *m);
void compass_model_begin(compass_model_t *m);
void compass_model_offer(compass_model_t *m, int x_mg, int y_mg);
int compass_model_finish(compass_model_t *m);
int compass_model_align(compass_model_t *m, int x_mg, int y_mg);
/* Align the marked board forward edge to any known magnetic bearing. */
int compass_model_align_reference(compass_model_t *m, int x_mg, int y_mg,
                                  int reference_deg10);
void compass_model_flip(compass_model_t *m);
void compass_model_cancel(compass_model_t *m);
void compass_model_confirm_direction(compass_model_t *m);
int compass_model_heading_deg10(const compass_model_t *m, int x_mg, int y_mg,
                                int *degrees10);
int compass_model_heading(const compass_model_t *m, int x_mg, int y_mg,
                          int *degrees);
int compass_model_span_x(const compass_model_t *m);
int compass_model_span_y(const compass_model_t *m);
/* Number of raw XY direction sectors seen during the current calibration. */
int compass_model_coverage(const compass_model_t *m);
/* Raw sensor XY field angle, not a bearing relative to north. */
int compass_model_preview_angle(int x_mg, int y_mg, int *degrees);

#endif
