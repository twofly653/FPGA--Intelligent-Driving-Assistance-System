#include "lsm6dsm.h"
#include "i2c_bitbang.h"
#include "xil_types.h"

#define WHO     0x0Fu
#define CTRL1   0x10u
#define CTRL2   0x11u
#define CTRL3   0x12u
#define OUTG    0x22u
#define STATUS  0x1Eu

static u8 g_addr = 0x6Bu;

static int16_t le16(const u8 *p)
{
    return (int16_t)((u16)p[0] | ((u16)p[1] << 8));
}

static int probe(u8 addr)
{
    u8 id;

    if (i2c_reg_read(addr, WHO, &id) != 0)
        return -1;
    if (id != 0x6A && id != 0x69) /* LSM6DSM / LSM6DS3 */
        return -1;
    g_addr = addr;
    return 0;
}

int lsm6_try_read(lsm6_sample_t *s)
{
    u8 status;
    if (!s || i2c_reg_read(g_addr, STATUS, &status) != 0)
        return -1;
    /* XLDA + GDA. Both ODRs are 104 Hz; don't fuse duplicate output values. */
    if ((status & 0x03u) != 0x03u)
        return 1;
    return lsm6_read(s);
}

int lsm6_init(void)
{
    if (probe(0x6Bu) != 0 && probe(0x6Au) != 0)
        return -1;
    /* BDU + auto-increment */
    if (i2c_reg_write(g_addr, CTRL3, 0x44) != 0)
        return -1;
    /* XL 104 Hz, ±2 g */
    if (i2c_reg_write(g_addr, CTRL1, 0x40) != 0)
        return -1;
    /* G  104 Hz, ±250 dps */
    if (i2c_reg_write(g_addr, CTRL2, 0x40) != 0)
        return -1;
    return 0;
}

int lsm6_read(lsm6_sample_t *s)
{
    u8 b[12];
    int16_t gx, gy, gz, ax, ay, az;

    if (!s)
        return -1;
    if (i2c_reg_read_n(g_addr, OUTG, b, 12) != 0)
        return -1;
    gx = le16(&b[0]);
    gy = le16(&b[2]);
    gz = le16(&b[4]);
    ax = le16(&b[6]);
    ay = le16(&b[8]);
    az = le16(&b[10]);
    s->acc_g[0] = (float)ax * 0.000061f;
    s->acc_g[1] = (float)ay * 0.000061f;
    s->acc_g[2] = (float)az * 0.000061f;
    s->gyro_dps[0] = (float)gx * 0.00875f;
    s->gyro_dps[1] = (float)gy * 0.00875f;
    s->gyro_dps[2] = (float)gz * 0.00875f;
    return 0;
}
