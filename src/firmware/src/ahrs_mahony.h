#ifndef AHRS_MAHONY_H
#define AHRS_MAHONY_H

typedef struct {
    float q0, q1, q2, q3;
    float twoKp, twoKi;
    float integralFBx, integralFBy, integralFBz;
    float roll, pitch, yaw; /* radians */
} ahrs_mahony_t;

void ahrs_init(ahrs_mahony_t *a);
/* gx,gy,gz in rad/s; ax,ay,az in g; mx,my,mz optional (0 = 6DOF) */
void ahrs_update(ahrs_mahony_t *a, float dt,
                 float gx, float gy, float gz,
                 float ax, float ay, float az,
                 float mx, float my, float mz, int use_mag);

#endif
