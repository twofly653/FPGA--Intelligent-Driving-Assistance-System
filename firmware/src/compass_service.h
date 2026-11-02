#ifndef COMPASS_SERVICE_H
#define COMPASS_SERVICE_H

#include "compass_model.h"

/* This service consumes the attitude owner's raw magnetometer cache. It never
 * performs I2C transactions and never uses relative ESKF yaw as magnetic north.
 * All consumers (LCD/UART/browser) use this one committed model and snapshot. */
#ifndef COMPASS_LEVEL_LIMIT_DEG
#define COMPASS_LEVEL_LIMIT_DEG 8.0f
#endif

typedef struct {
    float heading_deg;
    int heading_deg10;
    int valid, detected, fresh, calibrating, calibrated, aligned, level;
    float tilt_deg;
    int span_x, span_y, coverage, cal_samples;
    int raw_x_mg, raw_y_mg, raw_z_mg;
    int reference_deg10, direction, direction_verified;
    float fit_error;
    unsigned sequence;
    int field_ok, acceleration_ok;
    unsigned rejected_tilt_samples;
} compass_service_snapshot_t;

/* init explicitly clears RAM calibration; changing UI pages must not call it. */
void compass_service_init(void);
void compass_service_poll(void);
void compass_service_get_snapshot(compass_service_snapshot_t *snapshot);
const compass_model_t *compass_service_model(void);
/* Collection accepts spatially distinct XY points. The first trajectory
 * passing all fit/coverage gates is automatically committed and stops capture;
 * the operator may then wait arbitrarily before supplying the reference. */
void compass_service_begin_calibration(void);
/* Explicit END also tries the fit; already committed calibration returns 1. */
int compass_service_finish_calibration(void);
void compass_service_cancel_calibration(void);
/* Board's marked forward edge must match the reference's magnetic bearing.
 * A one-point reference fixes mounting offset, not chirality: check other axes. */
int compass_service_align_reference(int reference_deg10);
void compass_service_flip_direction(void);
void compass_service_confirm_direction(void);

#endif
