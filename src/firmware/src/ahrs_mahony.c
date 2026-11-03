#include "ahrs_mahony.h"
#include "math.h"

void ahrs_init(ahrs_mahony_t *a)
{
    a->q0 = 1.0f;
    a->q1 = a->q2 = a->q3 = 0.0f;
    a->twoKp = 2.0f * 2.0f;
    a->twoKi = 2.0f * 0.005f;
    a->integralFBx = a->integralFBy = a->integralFBz = 0.0f;
    a->roll = a->pitch = a->yaw = 0.0f;
}

static void normalize3(float *x, float *y, float *z)
{
    float n = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (n < 1e-6f)
        return;
    n = 1.0f / n;
    *x *= n;
    *y *= n;
    *z *= n;
}

void ahrs_update(ahrs_mahony_t *a, float dt,
                 float gx, float gy, float gz,
                 float ax, float ay, float az,
                 float mx, float my, float mz, int use_mag)
{
    float q0 = a->q0, q1 = a->q1, q2 = a->q2, q3 = a->q3;
    float hx, hy, bx, bz;
    float halfvx, halfvy, halfvz;
    float halfwx, halfwy, halfwz;
    float halfex, halfey, halfez;
    float qa, qb, qc;

    if (dt < 0.001f) dt = 0.001f;
    if (dt > 0.1f) dt = 0.1f;

    normalize3(&ax, &ay, &az);

    /* Estimated gravity direction */
    halfvx = q1 * q3 - q0 * q2;
    halfvy = q0 * q1 + q2 * q3;
    halfvz = q0 * q0 - 0.5f + q3 * q3;

    halfex = (ay * halfvz - az * halfvy);
    halfey = (az * halfvx - ax * halfvz);
    halfez = (ax * halfvy - ay * halfvx);

    if (use_mag) {
        float q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3;
        float q1q1 = q1 * q1, q1q2 = q1 * q2, q1q3 = q1 * q3;
        float q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;
        normalize3(&mx, &my, &mz);
        hx = 2.0f * (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) +
                     mz * (q1q3 + q0q2));
        hy = 2.0f * (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) +
                     mz * (q2q3 - q0q1));
        bx = sqrtf(hx * hx + hy * hy);
        bz = 2.0f * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) +
                     mz * (0.5f - q1q1 - q2q2));
        halfwx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
        halfwy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
        halfwz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);
        halfex += (my * halfwz - mz * halfwy);
        halfey += (mz * halfwx - mx * halfwz);
        halfez += (mx * halfwy - my * halfwx);
        (void)q0q0;
        (void)q0q1;
        (void)q1q1;
        (void)q2q2;
        (void)q3q3;
        (void)q1q2;
        (void)q2q3;
        (void)q0q2;
        (void)q0q3;
        (void)q1q3;
    }

    if (a->twoKi > 0.0f) {
        a->integralFBx += a->twoKi * halfex * dt;
        a->integralFBy += a->twoKi * halfey * dt;
        a->integralFBz += a->twoKi * halfez * dt;
        gx += a->integralFBx;
        gy += a->integralFBy;
        gz += a->integralFBz;
    } else {
        a->integralFBx = a->integralFBy = a->integralFBz = 0.0f;
    }

    gx += a->twoKp * halfex;
    gy += a->twoKp * halfey;
    gz += a->twoKp * halfez;

    gx *= (0.5f * dt);
    gy *= (0.5f * dt);
    gz *= (0.5f * dt);
    qa = q0;
    qb = q1;
    qc = q2;
    q0 += (-qb * gx - qc * gy - q3 * gz);
    q1 += (qa * gx + qc * gz - q3 * gy);
    q2 += (qa * gy - qb * gz + q3 * gx);
    q3 += (qa * gz + qb * gy - qc * gx);

    {
        float n = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
        if (n > 1e-6f) {
            n = 1.0f / n;
            a->q0 = q0 * n;
            a->q1 = q1 * n;
            a->q2 = q2 * n;
            a->q3 = q3 * n;
        }
    }

    /* ZYX Euler */
    a->roll = atan2f(2.0f * (a->q0 * a->q1 + a->q2 * a->q3),
                     1.0f - 2.0f * (a->q1 * a->q1 + a->q2 * a->q2));
    {
        float s = 2.0f * (a->q0 * a->q2 - a->q3 * a->q1);
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        a->pitch = asinf(s);
    }
    a->yaw = atan2f(2.0f * (a->q0 * a->q3 + a->q1 * a->q2),
                    1.0f - 2.0f * (a->q2 * a->q2 + a->q3 * a->q3));
}
