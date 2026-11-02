#ifndef PREVIEW_STUBS_H
#define PREVIEW_STUBS_H
#include "environment_service.h"
#include "attitude_est.h"
#include "collision_service.h"
#include "obstacle_service.h"
#include "xtime_l.h"
extern environment_snapshot_t preview_env;
extern attitude_est_snapshot_t preview_pose;
extern collision_model_t preview_collision;
extern obstacle_service_snapshot_t preview_obstacle;
extern obstacle_display_snapshot_t preview_display;
extern int preview_sensor_ready,preview_mic;
void preview_defaults(void);
#endif
