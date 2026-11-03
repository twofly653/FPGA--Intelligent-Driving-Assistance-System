#include "spa06.h"
#include "i2c_bitbang.h"
#include "sleep.h"
#include "xil_types.h"

#define SPA_ADDR  0x77u
#define KF        7864320.0f   /* 8x oversampling */

static float g_c00, g_c10, g_c20, g_c30, g_c40;
static float g_c01, g_c11, g_c21, g_c31;
static int   g_ok;

static int32_t sx(u32 v, int bits)
{
    u32 sign = 1u << (bits - 1);
    if (v & sign)
        return (int32_t)v - (int32_t)(1u << bits);
    return (int32_t)v;
}

static int32_t be24(const u8 *p)
{
    u32 v = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    return sx(v, 24);
}

int spa06_init(void)
{
    u8 id, meas, coef[21];
    u32 c00, c10, c31, c40;
    int32_t c0, c1, c01, c11, c20, c21, c30;
    int i;

    g_ok = 0;
    if (i2c_reg_read(SPA_ADDR, 0x0D, &id) != 0)
        return -1;
    /* SPA06-003 PROD_ID nibble is usually 0x1 (byte 0x11). Accept non-zero. */
    if ((id & 0x0Fu) != 0x01u)
        return -1;

    /* Wait COEF_RDY (MEAS_CFG bit7) */
    for (i = 0; i < 50; i++) {
        if (i2c_reg_read(SPA_ADDR, 0x08, &meas) != 0)
            return -1;
        if ((meas & 0xC0u) == 0xC0u)
            break;
        usleep(2000);
    }
    if ((meas & 0xC0u) != 0xC0u)
        return -1;

    if (i2c_reg_read_n(SPA_ADDR, 0x10, coef, 21) != 0)
        return -1;

    c0  = sx(((u32)coef[0] << 4) | (coef[1] >> 4), 12);
    c1  = sx((((u32)coef[1] & 0x0Fu) << 8) | coef[2], 12);
    c00 = ((u32)coef[3] << 12) | ((u32)coef[4] << 4) | (coef[5] >> 4);
    c10 = (((u32)coef[5] & 0x0Fu) << 16) | ((u32)coef[6] << 8) | coef[7];
    c01 = (int16_t)(((u16)coef[8] << 8) | coef[9]);
    c11 = (int16_t)(((u16)coef[10] << 8) | coef[11]);
    c20 = (int16_t)(((u16)coef[12] << 8) | coef[13]);
    c21 = (int16_t)(((u16)coef[14] << 8) | coef[15]);
    c30 = (int16_t)(((u16)coef[16] << 8) | coef[17]);
    c31 = ((u32)coef[18] << 4) | (coef[19] >> 4);
    c40 = (((u32)coef[19] & 0x0Fu) << 8) | coef[20];

    (void)c0;
    (void)c1;
    g_c00 = (float)sx(c00, 20);
    g_c10 = (float)sx(c10, 20);
    g_c20 = (float)c20;
    g_c30 = (float)c30;
    g_c40 = (float)sx(c40, 12);
    g_c01 = (float)c01;
    g_c11 = (float)c11;
    g_c21 = (float)c21;
    g_c31 = (float)sx(c31, 12);

    /* Reject all-zero coefficients (broken I2C read) */
    if (c00 == 0 && c10 == 0)
        return -1;

    /* PRS 8x OSR; TMP internal (TMP_EXT=0) 8x OSR; continuous P+T */
    if (i2c_reg_write(SPA_ADDR, 0x06, 0x23) != 0)
        return -1;
    if (i2c_reg_write(SPA_ADDR, 0x07, 0x23) != 0)
        return -1;
    if (i2c_reg_write(SPA_ADDR, 0x09, 0x00) != 0)
        return -1;
    if (i2c_reg_write(SPA_ADDR, 0x08, 0x07) != 0)
        return -1;

    usleep(40000);
    g_ok = 1;
    return 0;
}

static int spa06_decode(int *hpa)
{
    u8 raw[6];
    int32_t Praw, Traw;
    float Psc, Tsc, P2, P3, P4, Pcomp;

    if (i2c_reg_read_n(SPA_ADDR, 0x00, raw, 6) != 0)
        return -1;

    Praw = be24(&raw[0]);
    Traw = be24(&raw[3]);
    if (Praw == 0 && Traw == 0)
        return -1;

    Psc = (float)Praw / KF;
    Tsc = (float)Traw / KF;
    P2 = Psc * Psc;
    P3 = P2 * Psc;
    P4 = P2 * P2;
    Pcomp = g_c00 + g_c10 * Psc + g_c20 * P2 + g_c30 * P3 + g_c40 * P4
          + Tsc * (g_c01 + g_c11 * Psc + g_c21 * P2 + g_c31 * P3);
    if (hpa) {
        *hpa = (int)(Pcomp / 100.0f + 0.5f);
        if (*hpa < 0)
            *hpa = 0;
        if (*hpa > 9999)
            *hpa = 9999;
    }
    return 0;
}

int spa06_try_read_hpa(int *hpa)
{
    u8 meas;

    if (!g_ok)
        return -1;
    if (i2c_reg_read(SPA_ADDR, 0x08, &meas) != 0)
        return -1;
    if (!(meas & 0x10u))
        return -1;
    return spa06_decode(hpa);
}

int spa06_read_hpa(int *hpa)
{
    u8 meas;
    int i;

    if (!g_ok)
        return -1;

    for (i = 0; i < 8; i++) {
        if (i2c_reg_read(SPA_ADDR, 0x08, &meas) != 0)
            return -1;
        if (meas & 0x10u)
            return spa06_decode(hpa);
        usleep(1000);
    }
    return -1;
}
