#include "pl_hub.h"

u32 pl_status(void)
{
    return pl_rd(REG_STATUS);
}

int pl_btn_pressed(int i)
{
    if (i < 0 || i > 3)
        return 0;
    return (pl_rd(REG_STATUS) & ST_BTN_STICKY(i)) ? 1 : 0;
}

void pl_btn_clear(int i)
{
    if (i < 0 || i > 3)
        return;
    pl_wr(REG_STATUS_CLR, ST_BTN_STICKY(i));
}

/* compat wrappers used by older code */
int pl_btn0_pressed(void)
{
    return pl_btn_pressed(0);
}

void pl_btn0_clear(void)
{
    pl_btn_clear(0);
}

void seg_set_bright(u8 pct)
{
    if (pct > 100)
        pct = 100;
    pl_wr(REG_SEG_BRIGHT, pct);
}

void seg_blank_all(void)
{
    pl_wr(REG_SEG_HEX0, 0);
    pl_wr(REG_SEG_HEX1, 0);
    pl_wr(REG_SEG_BLANK, 0x1FFu);
    seg_set_bright(0);
}

void seg_show_boot(void)
{
    seg_show_time(0xA, 0, 0, 0);
}

/*
 * letter_hex: 0xA / 0xB / 0xC on leftmost digit
 * hh 00-99, mm/ss 00-59
 */
void seg_show_time(u8 letter_hex, u8 hh, u8 mm, u8 ss)
{
    u32 hex0;

    if (hh > 99) hh = 99;
    if (mm > 59) mm = 59;
    if (ss > 59) ss = 59;

    hex0  = (u32)(letter_hex & 0xF);
    hex0 |= ((u32)(hh / 10) & 0xF) << 4;
    hex0 |= ((u32)(hh % 10) & 0xF) << 8;
    hex0 |= ((u32)(mm / 10) & 0xF) << 12;
    hex0 |= ((u32)(mm % 10) & 0xF) << 16;
    hex0 |= ((u32)(ss / 10) & 0xF) << 20;
    hex0 |= ((u32)(ss % 10) & 0xF) << 24;

    pl_wr(REG_SEG_HEX0, hex0);
    pl_wr(REG_SEG_HEX1, 0);
    pl_wr(REG_SEG_BLANK, (1u << 7) | (1u << 8));
    if ((pl_rd(REG_SEG_BRIGHT) & 0xFF) == 0)
        seg_set_bright(50);
}

void seg_show_digits(const u8 digits[9], u16 blank_mask)
{
    u32 hex0 = 0;
    int i;

    for (i = 0; i < 8; ++i)
        hex0 |= ((u32)(digits[i] & 0x0Fu)) << (4 * i);
    pl_wr(REG_SEG_HEX0, hex0);
    pl_wr(REG_SEG_HEX1, (u32)(digits[8] & 0x0Fu));
    pl_wr(REG_SEG_BLANK, (u32)blank_mask & 0x1FFu);
    if ((pl_rd(REG_SEG_BRIGHT) & 0xFFu) == 0)
        seg_set_bright(50);
}

void buz_beep(u16 hz, u16 ms)
{
    pl_wr(REG_BUZ_FREQ, hz);
    pl_wr(REG_BUZ_MS, ms);
    pl_wr(REG_BUZ_CTRL, 1);
}

void ws_set_color(u32 grb)
{
    pl_wr(REG_WS_COLOR, grb);
}

void ws_set_bright(u8 pct)
{
    if (pct > 100)
        pct = 100;
    pl_wr(REG_WS_BRIGHT, pct);
}

static u32 g_ws_last_mode;

void ws_start(u32 mode)
{
    g_ws_last_mode = mode & 0x7u;
    pl_wr(REG_WS_CTRL, g_ws_last_mode);
}

u32 ws_last_mode(void)
{
    return g_ws_last_mode;
}

void ws_wait_idle(void)
{
    int timeout = 0;

    while (pl_rd(REG_STATUS) & ST_WS_BUSY) {
        if (++timeout > 5000000)
            break;
    }
}

void lcd_gpio_write(u32 val)
{
    pl_wr(REG_LCD_GPIO, val & 0x3Fu);
}

u32 lcd_gpio_read(void)
{
    return pl_rd(REG_LCD_GPIO) & 0x3Fu;
}

void i2c_gpio_write(u32 val)
{
    pl_wr(REG_I2C_GPIO, val & 0x3u);
}

u32 i2c_gpio_read(void)
{
    return pl_rd(REG_I2C_GPIO);
}

u8 mic_db(void)
{
    return (u8)(pl_rd(REG_MIC_STAT) & 0xFFu);
}

u16 mic_amp(void)
{
    return (u16)((pl_rd(REG_MIC_STAT) >> 8) & 0xFFFFu);
}

u32 pl_key_stat(void)
{
    return pl_rd(REG_KEY_STAT);
}

u32 pl_enc_stat(void)
{
    return pl_rd(REG_ENC_STAT);
}

void pl_key_clear(u32 mask_nswec)
{
    pl_wr(REG_INPUT_CLR, mask_nswec & 0x1Fu);
}

void pl_enc_clear(u32 mask)
{
    pl_wr(REG_INPUT_CLR, mask & (ENC_ST_CW | ENC_ST_CCW | ENC_ST_PB));
}

void pl_tp_rst(int high)
{
    pl_wr(REG_TP_GPIO, high ? 1u : 0u);
}

int pl_tp_int_level(void)
{
    return (int)((pl_rd(REG_TP_GPIO) >> 8) & 1u);
}
