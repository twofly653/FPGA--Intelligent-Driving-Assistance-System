#include "i2c_bitbang.h"
#include "pl_hub.h"
#include "xtime_l.h"

/* oe=1 drives line low; oe=0 releases (PULLUP -> 1) */
#define OE_SCL  (1u << 0)
#define OE_SDA  (1u << 1)

static u32 g_oe;

static void i2c_delay(void)
{
    XTime start, now;
    /* AXI reads have variable latency: 200 reads was not a 5 us delay.
     * Use the A9 global timer and retain conservative standard-mode timing.
     * No callbacks here: every sensor shares this non-reentrant I2C bus. */
    const XTime ticks = (COUNTS_PER_SECOND + 199999u) / 200000u;
    XTime_GetTime(&start);
    do { XTime_GetTime(&now); } while (now - start < ticks);
}

static void i2c_commit(void)
{
    i2c_gpio_write(g_oe);
    (void)i2c_gpio_read(); /* Drain the posted write before measuring setup time. */
    i2c_delay();
}

static void scl_hi(void) { g_oe &= ~OE_SCL; i2c_commit(); }
static void scl_lo(void) { g_oe |=  OE_SCL; i2c_commit(); }
static void sda_hi(void) { g_oe &= ~OE_SDA; i2c_commit(); }
static void sda_lo(void) { g_oe |=  OE_SDA; i2c_commit(); }

static int sda_read(void)
{
    return (i2c_gpio_read() & 2) ? 1 : 0;
}

static void i2c_start(void)
{
    sda_hi();
    scl_hi();
    sda_lo();
    scl_lo();
}

static void i2c_stop(void)
{
    sda_lo();
    scl_hi();
    sda_hi();
}

static int i2c_write_byte(u8 b)
{
    int i, ack;

    for (i = 7; i >= 0; i--) {
        if (b & (1u << i))
            sda_hi();
        else
            sda_lo();
        scl_hi();
        scl_lo();
    }
    sda_hi();
    scl_hi();
    ack = sda_read();
    scl_lo();
    return ack ? -1 : 0;
}

static u8 i2c_read_byte(int nack)
{
    int i;
    u8 b = 0;

    sda_hi();
    for (i = 7; i >= 0; i--) {
        scl_hi();
        if (sda_read())
            b |= (u8)(1u << i);
        scl_lo();
    }
    if (nack)
        sda_hi();
    else
        sda_lo();
    scl_hi();
    scl_lo();
    sda_hi();
    return b;
}

void i2c_init(void)
{
    int i;
    g_oe = 0;
    i2c_commit();
    /* A CPU/PL reload can interrupt a peripheral between data bits. Release
     * SDA, clock at most nine bits, then STOP before probing any device.
     * NXP UM10204 section 3.1.16; never actively drive either line high. */
    if (i2c_gpio_read() & 1u) {
        for (i = 0; i < 9; ++i) {
            scl_lo();
            scl_hi();
            if (!(i2c_gpio_read() & 1u)) break; /* Cannot clear a held-low SCL. */
        }
        i2c_stop();
    }
    g_oe = 0;
    i2c_commit();
}

int i2c_probe(u8 addr7)
{
    int ack;

    i2c_start();
    ack = i2c_write_byte((u8)(addr7 << 1));
    i2c_stop();
    return ack;
}

int i2c_write(u8 addr7, const u8 *data, int n)
{
    int i;

    i2c_start();
    if (i2c_write_byte((u8)(addr7 << 1)) != 0) {
        i2c_stop();
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (i2c_write_byte(data[i]) != 0) {
            i2c_stop();
            return -1;
        }
    }
    i2c_stop();
    return 0;
}

int i2c_read(u8 addr7, u8 *data, int n)
{
    int i;

    i2c_start();
    if (i2c_write_byte((u8)((addr7 << 1) | 1u)) != 0) {
        i2c_stop();
        return -1;
    }
    for (i = 0; i < n; i++)
        data[i] = i2c_read_byte(i == n - 1);
    i2c_stop();
    return 0;
}

int i2c_write_read(u8 addr7, const u8 *wr, int nw, u8 *rd, int nr)
{
    int i;

    i2c_start();
    if (i2c_write_byte((u8)(addr7 << 1)) != 0) {
        i2c_stop();
        return -1;
    }
    for (i = 0; i < nw; i++) {
        if (i2c_write_byte(wr[i]) != 0) {
            i2c_stop();
            return -1;
        }
    }
    i2c_start();
    if (i2c_write_byte((u8)((addr7 << 1) | 1u)) != 0) {
        i2c_stop();
        return -1;
    }
    for (i = 0; i < nr; i++)
        rd[i] = i2c_read_byte(i == nr - 1);
    i2c_stop();
    return 0;
}

int i2c_reg_write(u8 addr7, u8 reg, u8 val)
{
    u8 b[2];
    b[0] = reg;
    b[1] = val;
    return i2c_write(addr7, b, 2);
}

int i2c_reg_read(u8 addr7, u8 reg, u8 *val)
{
    return i2c_write_read(addr7, &reg, 1, val, 1);
}

int i2c_reg_read_n(u8 addr7, u8 reg, u8 *buf, int n)
{
    return i2c_write_read(addr7, &reg, 1, buf, n);
}
