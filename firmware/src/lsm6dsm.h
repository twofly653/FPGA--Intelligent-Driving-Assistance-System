#ifndef LSM6DSM_H
#define LSM6DSM_H

typedef struct {
    float acc_g[3];
    float gyro_dps[3];
} lsm6_sample_t;

int lsm6_init(void);
int lsm6_read(lsm6_sample_t *s);
/* 0 = a new coherent accel+gyro sample; 1 = not ready; -1 = I2C error. */
int lsm6_try_read(lsm6_sample_t *s);

#endif
