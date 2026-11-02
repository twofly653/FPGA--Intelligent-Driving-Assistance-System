#include "veml3328.h"
#include "i2c_bitbang.h"
#include "sleep.h"

/* Schematic 8-bit addr 0x20 → 7-bit 0x10 */
#define VEML_ADDR   0x10u
#define VEML_CONF   0x00u
#define VEML_C      0x04u
#define VEML_R      0x05u
#define VEML_G      0x06u
#define VEML_B      0x07u
#define VEML_IR     0x08u
#define VEML_ID     0x0Cu

/*
 * Vishay AN: lux = G_DATA * sensitivity
 * IT=50 ms, GAIN=x1, DG=x1 → sensitivity = 0.384 lx/count
 */
#define VEML_LUX_NUM   384u
#define VEML_LUX_DEN   1000u

static int read_u16(u8 reg, u16 *out)
{
    u8 b[2];

    if (i2c_reg_read_n(VEML_ADDR, reg, b, 2) != 0)
        return -1;
    *out = (u16)b[0] | ((u16)b[1] << 8);
    return 0;
}

static void fill_lux(veml_sample_t *s)
{
    s->lux = ((u32)s->g * VEML_LUX_NUM) / VEML_LUX_DEN;
}

int veml3328_init(void)
{
    u16 id;
    u8 wr[3];

    if (read_u16(VEML_ID, &id) != 0 || (id & 0xFFu) != 0x28u)
        return -1;

    /* CONF=0: SD0/SD1=0 power on, IT=50ms, DG=1x, GAIN=1x */
    wr[0] = VEML_CONF;
    wr[1] = 0x00;
    wr[2] = 0x00;
    if (i2c_write(VEML_ADDR, wr, 3) != 0)
        return -1;
    usleep(100000);
    return 0;
}

int veml3328_read(veml_sample_t *s)
{
    veml_sample_t next;
    if (!s)
        return -1;
    if (read_u16(VEML_C, &next.clear) != 0)
        return -1;
    if (read_u16(VEML_R, &next.r) != 0)
        return -1;
    if (read_u16(VEML_G, &next.g) != 0)
        return -1;
    if (read_u16(VEML_B, &next.b) != 0)
        return -1;
    if (read_u16(VEML_IR, &next.ir) != 0)
        return -1;
    fill_lux(&next);
    *s = next;
    return 0;
}

int veml3328_read_lux_channels(veml_sample_t *s)
{
    veml_sample_t next;
    if (!s)
        return -1;
    next = *s;
    if (read_u16(VEML_C, &next.clear) != 0)
        return -1;
    if (read_u16(VEML_G, &next.g) != 0)
        return -1;
    fill_lux(&next);
    *s = next;
    return 0;
}
