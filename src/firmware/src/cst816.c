#include "cst816.h"
#include "i2c_bitbang.h"
#include "pl_hub.h"
#include "lcd_st7789.h"
#include "xil_printf.h"
#include "sleep.h"
#include "xtime_l.h"

#define CST_REG_TOUCH_NUM  0x02u
#define CST_REG_XH         0x03u

#ifndef CST816_FLIP_Y
#define CST816_FLIP_Y  1
#endif
#ifndef CST816_MAP_H
#define CST816_MAP_H   280
#endif

static int g_ok;
static int g_fired;
static XTime g_poll_tick;
static unsigned g_reads, g_errors;

int cst816_ok(void)
{
    return g_ok;
}

int cst816_init(void)
{
    u8 n;

    g_ok = 0;
    g_fired = 0;
    g_poll_tick = 0;
    g_reads = g_errors = 0;

    pl_tp_rst(0);
    usleep(10000);
    pl_tp_rst(1);
    usleep(50000);

    if (i2c_probe(CST816_ADDR7) != 0) {
        xil_printf("touch: CST816 not found @0x15 (keys still work)\r\n");
        return -1;
    }
    if (i2c_reg_read(CST816_ADDR7, CST_REG_TOUCH_NUM, &n) != 0) {
        xil_printf("touch: read fail\r\n");
        return -1;
    }
    g_ok = 1;
    xil_printf("touch: CST816 ok\r\n");
    return 0;
}

int cst816_poll_press(u16 *x, u16 *y)
{
    u8 buf[5];
    XTime now;
    u16 rx, ry;
    int down;

    if (!g_ok || !x || !y)
        return 0;

    XTime_GetTime(&now);
    if (g_poll_tick && now - g_poll_tick < COUNTS_PER_SECOND / 200u)
        return 0;
    g_poll_tick = now;
    ++g_reads;
    /* One coherent 0x02..0x06 frame, instead of two I2C transactions.
     * Keep polling as fallback: the module's INT wire may be absent. */
    if (i2c_reg_read_n(CST816_ADDR7, CST_REG_TOUCH_NUM, buf, 5) != 0) {
        ++g_errors;
        return 0;
    }
    down = ((buf[0] & 0x0Fu) > 0);

    if (!down) {
        g_fired = 0;
        return 0;
    }

    rx = (u16)(((buf[1] & 0x0Fu) << 8) | buf[2]);
    ry = (u16)(((buf[3] & 0x0Fu) << 8) | buf[4]);
    /* Reject off-panel samples; clamping could activate an edge control. */
    if (rx >= LCD_W || ry >= CST816_MAP_H)
        return 0;

#if CST816_FLIP_Y
    if (ry < CST816_MAP_H)
        ry = (u16)(CST816_MAP_H - 1 - ry);
#endif
    *x = rx;
    *y = ry;

    if (!g_fired)
        return 1;
    return 0;
}

void cst816_mark_handled(void)
{
    g_fired = 1;
}

void cst816_stats(unsigned *reads, unsigned *errors)
{
    if (reads) *reads = g_reads;
    if (errors) *errors = g_errors;
}
