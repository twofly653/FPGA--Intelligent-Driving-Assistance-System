#ifndef COLLISION_SERVICE_H
#define COLLISION_SERVICE_H

#include "collision_model.h"

/* Initialise after the shared I2C GPIO has been initialised. */
void collision_service_init(void);
/* Call from the main UI loop; returns 1 when a new range result arrived. */
int collision_service_poll(void);
const collision_model_t *collision_service_state(void);
int collision_service_sensor_ready(void);

typedef enum {
    COLLISION_RECOVERY_NONE=0, COLLISION_RECOVERY_INIT=1,
    COLLISION_RECOVERY_IO=2, COLLISION_RECOVERY_NO_FRAME=3,
    COLLISION_RECOVERY_NOT_RUNNING=4
} collision_recovery_reason_t;
typedef struct {
    int ready,last_return,last_init_result,last_api_status,last_io_error;
    int range_status,raw_mm,recovery_reason,recovery_pending;
    uint32_t signal_mcps_x65536,ambient_mcps_x65536;
    uint32_t last_poll_ms,last_frame_ms,last_valid_ms;
    uint32_t poll_age_ms,frame_age_ms,valid_age_ms;
    uint32_t poll_count,frame_count,valid_count,not_ready_count;
    uint32_t io_error_count,invalid_count,not_running_count;
    uint32_t init_failure_count,recovery_count,retry_in_ms;
} collision_service_diagnostic_t;
/* UINT32_MAX age means no event. raw_mm is diagnostic, never a valid range. */
void collision_service_get_diagnostic(collision_service_diagnostic_t *out);
#endif
