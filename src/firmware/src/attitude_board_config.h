#ifndef ATTITUDE_BOARD_CONFIG_H
#define ATTITUDE_BOARD_CONFIG_H

/* Allow a stationary sensor's zero-rate offset to be estimated. This cap
 * alone does NOT prove stillness: the variance/direction/window checks and
 * the operator's instruction to keep the board still remain necessary. */
#define ATTITUDE_ALIGN_GYRO_LIMIT_DPS 15.0f

/* Verify on the assembled board before describing axes as vehicle axes.
 * The default identity mapping preserves the sensor's own right-handed axes.
 * The EKF requires body X forward, Y left, Z up; acceleration is +1 g on Z
 * when that body frame is level. Apply the SAME IMU map to accel and gyro.
 */
static const float attitude_imu_to_body[3][3] = {
    {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
};
static const float attitude_mag_to_body[3][3] = {
    {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
};

/* Leave disabled until a THREE-AXIS calibration and sensor-axis alignment
 * have been measured. The compass page's planar XY calibration is not enough.
 * calibrated_sensor = soft_iron * (raw_sensor_mg - hard_iron_mg).
 * Then apply attitude_mag_to_body. Calibration alone does not reject local
 * magnetic interference: inspect measurements on the assembled vehicle.
 */
#define ATTITUDE_BOARD_MAG_CALIBRATED 0
static const float attitude_mag_hard_iron_mg[3] = {0.0f, 0.0f, 0.0f};
static const float attitude_mag_soft_iron[3][3] = {
    {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
};

#endif
