#include "hdc1080.h"
#include "i2c_bitbang.h"
#include "sleep.h"

#define HDC_ADDR  0x40u
#define HDC_TEMP  0x00u
#define HDC_CFG   0x02u

int hdc1080_init(void)
{
    u8 cfg[3];

    cfg[0] = HDC_CFG;
    cfg[1] = 0x10;
    cfg[2] = 0x00;
    if (i2c_write(HDC_ADDR, cfg, 3) != 0)
        return -1;
    usleep(15000);
    return 0;
}

int hdc1080_start(void)
{
    u8 ptr = HDC_TEMP;
    return i2c_write(HDC_ADDR, &ptr, 1);
}

int hdc1080_fetch(int *temp_c, int *rh_pct)
{
    u8 d[4];
    u32 raw_t, raw_h;

    if (i2c_read(HDC_ADDR, d, 4) != 0)
        return -1;

    raw_t = ((u32)d[0] << 8) | d[1];
    raw_h = ((u32)d[2] << 8) | d[3];
    if (raw_t == 0 && raw_h == 0)
        return -1;

    if (temp_c)
        *temp_c = (int)((raw_t * 1650u) / 65536u) - 400;
    if (rh_pct) {
        *rh_pct = (int)((raw_h * 1000u) / 65536u);
        if (*rh_pct > 999)
            *rh_pct = 999;
        if (*rh_pct < 0)
            *rh_pct = 0;
    }
    return 0;
}

int hdc1080_read(int *temp_c, int *rh_pct)
{
    if (hdc1080_start() != 0)
        return -1;
    usleep(15000);
    return hdc1080_fetch(temp_c, rh_pct);
}
