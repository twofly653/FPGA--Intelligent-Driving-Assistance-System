#include "lcd_st7789.h"
#include "lcd_gpio.h"
#include "pl_hub.h"
#include "font_lcd.h"
#include "sleep.h"
#include "image3.h"
#include "image2.h"
#include "xtime_l.h"
#include <string.h>

#define WP_HDR     8
#define WP_W       240
#define WP_HOME_H  284 /* image3.h */
#define WP_SUB_H   280 /* image2.h */

static void lcd_reset_queue(void);

static void lcd_wr_reg(u8 reg)
{
    lcd_gpio_set(LCD_GPIO_CS, 0);
    lcd_gpio_set(LCD_GPIO_DC, 0);
    lcd_spi_write_byte(reg);
    lcd_gpio_set(LCD_GPIO_CS, LCD_GPIO_CS);
}

static void lcd_wr_data8(u8 dat)
{
    lcd_gpio_set(LCD_GPIO_CS, 0);
    lcd_gpio_set(LCD_GPIO_DC, LCD_GPIO_DC);
    lcd_spi_write_byte(dat);
    lcd_gpio_set(LCD_GPIO_CS, LCD_GPIO_CS);
}

static void lcd_address_set(u16 x1, u16 y1, u16 x2, u16 y2)
{
    lcd_wr_reg(0x2A);
    lcd_wr_data8((u8)(x1 >> 8));
    lcd_wr_data8((u8)(x1 & 0xFF));
    lcd_wr_data8((u8)(x2 >> 8));
    lcd_wr_data8((u8)(x2 & 0xFF));
    lcd_wr_reg(0x2B);
    lcd_wr_data8((u8)(y1 >> 8));
    lcd_wr_data8((u8)(y1 & 0xFF));
    lcd_wr_data8((u8)(y2 >> 8));
    lcd_wr_data8((u8)(y2 & 0xFF));
    lcd_wr_reg(0x2C);
}

void lcd_init(void)
{
    lcd_gpio_init();
    lcd_reset_queue();

    lcd_gpio_set(LCD_GPIO_RST, 0);
    usleep(100000);
    lcd_gpio_set(LCD_GPIO_RST, LCD_GPIO_RST);
    usleep(100000);
    lcd_gpio_set(LCD_GPIO_BLK, LCD_GPIO_BLK);
    usleep(100000);

    lcd_wr_reg(0x11);
    usleep(120000);

    lcd_wr_reg(0x36);
    lcd_wr_data8(0x00);
    lcd_wr_reg(0x3A);
    lcd_wr_data8(0x05);
    lcd_wr_reg(0xB2);
    lcd_wr_data8(0x0C);
    lcd_wr_data8(0x0C);
    lcd_wr_data8(0x00);
    lcd_wr_data8(0x33);
    lcd_wr_data8(0x33);
    lcd_wr_reg(0xB7);
    lcd_wr_data8(0x35);
    lcd_wr_reg(0xBB);
    lcd_wr_data8(0x32);
    lcd_wr_reg(0xC2);
    lcd_wr_data8(0x01);
    lcd_wr_reg(0xC3);
    lcd_wr_data8(0x15);
    lcd_wr_reg(0xC4);
    lcd_wr_data8(0x20);
    lcd_wr_reg(0xC6);
    lcd_wr_data8(0x0F);
    lcd_wr_reg(0xD0);
    lcd_wr_data8(0xA4);
    lcd_wr_data8(0xA1);
    lcd_wr_reg(0xE0);
    lcd_wr_data8(0xD0);
    lcd_wr_data8(0x08);
    lcd_wr_data8(0x0E);
    lcd_wr_data8(0x09);
    lcd_wr_data8(0x09);
    lcd_wr_data8(0x05);
    lcd_wr_data8(0x31);
    lcd_wr_data8(0x33);
    lcd_wr_data8(0x48);
    lcd_wr_data8(0x17);
    lcd_wr_data8(0x14);
    lcd_wr_data8(0x15);
    lcd_wr_data8(0x31);
    lcd_wr_data8(0x34);
    lcd_wr_reg(0xE1);
    lcd_wr_data8(0xD0);
    lcd_wr_data8(0x08);
    lcd_wr_data8(0x0E);
    lcd_wr_data8(0x09);
    lcd_wr_data8(0x09);
    lcd_wr_data8(0x15);
    lcd_wr_data8(0x31);
    lcd_wr_data8(0x33);
    lcd_wr_data8(0x48);
    lcd_wr_data8(0x17);
    lcd_wr_data8(0x14);
    lcd_wr_data8(0x15);
    lcd_wr_data8(0x31);
    lcd_wr_data8(0x34);
    lcd_wr_reg(0x21);
    lcd_wr_reg(0x29);

    /* Clear full visible area (incl. bottom pad beyond 280 wallpaper). */
    lcd_fill(0, 0, LCD_W, LCD_H, COLOR_BLACK);
}


static u16 lcd_fb[LCD_W * LCD_H] __attribute__((aligned(4)));
/* CPU-owned shadow: no DMA/cache maintenance is involved. Compare final
 * transaction pixels, so clearing/repainting an unchanged card costs no SPI. */
static u16 lcd_sent_fb[LCD_W * LCD_H] __attribute__((aligned(4)));
static void (*g_idle_hook)(void);
static int g_idle_hook_busy;
static int g_defer;
#define TILE_W 16u
#define TILE_H 8u
#define TILE_COLS ((LCD_W + TILE_W - 1u) / TILE_W)
#define TILE_ROWS ((LCD_H + TILE_H - 1u) / TILE_H)
#define TILE_COUNT (TILE_COLS * TILE_ROWS)
static u8 g_dirty_tiles[TILE_COUNT];
static u8 g_sent_tiles[TILE_COUNT];
static unsigned g_next_tile, g_pending_tiles, g_max_slice_us;

static void lcd_reset_queue(void)
{
    memset(g_dirty_tiles, 0, sizeof g_dirty_tiles);
    memset(g_sent_tiles, 0, sizeof g_sent_tiles);
    g_next_tile = g_pending_tiles = g_max_slice_us = 0;
    g_defer = 0;
    g_idle_hook_busy = 0;
}

void lcd_set_idle_hook(void (*hook)(void))
{
    g_idle_hook = hook;
}

static void lcd_service_sensors(void)
{
    if (g_idle_hook && !g_idle_hook_busy) {
        g_idle_hook_busy = 1;
        g_idle_hook();
        g_idle_hook_busy = 0;
    }
}

static void dirty_add(u16 x0, u16 y0, u16 x1, u16 y1)
{
    if (x0 >= x1 || y0 >= y1)
        return;
    {
        unsigned tx, ty;
        if (x1 > LCD_W) x1 = LCD_W;
        if (y1 > LCD_H) y1 = LCD_H;
        for (ty = y0 / TILE_H; ty <= (y1 - 1u) / TILE_H; ++ty) {
            for (tx = x0 / TILE_W; tx <= (x1 - 1u) / TILE_W; ++tx) {
                unsigned tile = ty * TILE_COLS + tx;
                if (!g_dirty_tiles[tile]) {
                    g_dirty_tiles[tile] = 1;
                    ++g_pending_tiles;
                }
            }
        }
        /* Do not reset the round-robin cursor on repaint: a frequently
         * redrawn horizon must not starve the numeric cards below it. */
    }
}

static void lcd_flush_rect(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u16 y;

    if (x1 > LCD_W) x1 = LCD_W;
    if (y1 > LCD_H) y1 = LCD_H;
    if (x1 <= x0 || y1 <= y0)
        return;

    lcd_address_set(x0, y0, x1 - 1, y1 - 1);
    lcd_gpio_set(LCD_GPIO_CS, 0);
    lcd_gpio_set(LCD_GPIO_DC, LCD_GPIO_DC);

    if (x0 == 0 && x1 == LCD_W) {
        for (y = y0; y < y1; ) {
            u16 rows = (u16)(y1 - y);
            if (rows > 4u) rows = 4u;
            lcd_spi_burst_u16(lcd_fb + (u32)y * LCD_W,
                              (u32)rows * LCD_W);
            y = (u16)(y + rows);
        }
    } else {
        for (y = y0; y < y1; y++) {
            lcd_spi_burst_u16(lcd_fb + (u32)y * LCD_W + x0,
                              (u32)(x1 - x0));
        }
    }

    lcd_gpio_set(LCD_GPIO_CS, LCD_GPIO_CS);
}

void lcd_poll(void)
{
    XTime start, now;
    unsigned scanned = 0, elapsed;
    if (g_defer || !g_pending_tiles) return;
    XTime_GetTime(&start);
    while (g_pending_tiles && scanned < TILE_COUNT) {
        unsigned tile = g_next_tile;
        g_next_tile = (g_next_tile + 1u) % TILE_COUNT;
        ++scanned;
        if (!g_dirty_tiles[tile]) continue;
        g_dirty_tiles[tile] = 0;
        --g_pending_tiles;
        {
            u16 x = (u16)((tile % TILE_COLS) * TILE_W);
            u16 y = (u16)((tile / TILE_COLS) * TILE_H);
            u16 row, end_y = (u16)(y + TILE_H);
            int changed = !g_sent_tiles[tile];
            if (end_y > LCD_H) end_y = LCD_H;
            if (!changed) {
                for (row = y; row < end_y; ++row) {
                    u32 offset = (u32)row * LCD_W + x;
                    if (memcmp(lcd_fb + offset, lcd_sent_fb + offset,
                               TILE_W * sizeof(u16)) != 0) {
                        changed = 1;
                        break;
                    }
                }
            }
            if (changed) {
                lcd_flush_rect(x, y, (u16)(x + TILE_W), end_y);
                for (row = y; row < end_y; ++row) {
                    u32 offset = (u32)row * LCD_W + x;
                    memcpy(lcd_sent_fb + offset, lcd_fb + offset,
                           TILE_W * sizeof(u16));
                }
                g_sent_tiles[tile] = 1;
            }
        }
        /* Capture inputs and sample sensors without dispatching/re-entering
         * drawing. Each SPI block is at most 16x8 pixels. */
        lcd_service_sensors();
        XTime_GetTime(&now);
        if (now - start >= COUNTS_PER_SECOND / 500u) break;
    }
    XTime_GetTime(&now);
    elapsed = (unsigned)(((now - start) * 1000000ULL) / COUNTS_PER_SECOND);
    if (elapsed > g_max_slice_us) g_max_slice_us = elapsed;
}

void lcd_get_stats(unsigned *pending, unsigned *max_slice_us)
{
    if (pending) *pending = g_pending_tiles;
    if (max_slice_us) *max_slice_us = g_max_slice_us;
}

void lcd_defer_start(void)
{
    g_defer++;
}

void lcd_defer_end(void)
{
    if (g_defer > 0)
        g_defer--;
}

static void fb_mark(u16 x0, u16 y0, u16 x1, u16 y1)
{
    dirty_add(x0, y0, x1, y1);
}


/* Coverage masks use the same CPU framebuffer and bounded dirty-tile queue. */
void lcd_blend_mask(u16 x, u16 y, u16 w, u16 h, const u8 *mask, u16 fg, u16 bg)
{
    unsigned yy, xx;
    if (!mask || x >= LCD_W || y >= LCD_H) return;
    for (yy = 0; yy < h && y + yy < LCD_H; ++yy) {
        for (xx = 0; xx < w && x + xx < LCD_W; ++xx) {
            unsigned a = mask[yy * w + xx];
            u16 *dst = &lcd_fb[(y + yy) * LCD_W + x + xx];
            u16 base = bg == COLOR_TRANSPARENT ? *dst : bg;
            if (!a) { if (bg != COLOR_TRANSPARENT) *dst = bg; }
            else if (a == 255u) *dst = fg;
            else {
                unsigned inv = 255u - a;
                unsigned r = (((fg >> 11) & 31u) * a + ((base >> 11) & 31u) * inv + 127u) / 255u;
                unsigned g = (((fg >> 5) & 63u) * a + ((base >> 5) & 63u) * inv + 127u) / 255u;
                unsigned b = ((fg & 31u) * a + (base & 31u) * inv + 127u) / 255u;
                *dst = (u16)((r << 11) | (g << 5) | b);
            }
        }
    }
    fb_mark(x, y, (u16)(x + w), (u16)(y + h));
}
#ifdef UI_HOST_PREVIEW
const u16 *lcd_host_framebuffer(void) { return lcd_fb; }
#endif

void lcd_fill(u16 x0, u16 y0, u16 x1, u16 y1, u16 color)
{
    u16 x, y;

    if (x1 > LCD_W) x1 = LCD_W;
    if (y1 > LCD_H) y1 = LCD_H;
    if (x1 <= x0 || y1 <= y0)
        return;

    for (y = y0; y < y1; y++) {
        u16 *row = lcd_fb + (u32)y * LCD_W + x0;
        for (x = 0; x < (u16)(x1 - x0); x++)
            row[x] = color;
    }
    fb_mark(x0, y0, x1, y1);
}

void lcd_line(int x0, int y0, int x1, int y1, u16 color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int minx = LCD_W, miny = LCD_H, maxx = -1, maxy = -1;

    for (;;) {
        if (x0 >= 0 && x0 < LCD_W && y0 >= 0 && y0 < LCD_H) {
            lcd_fb[(u32)y0 * LCD_W + (u32)x0] = color;
            if (x0 < minx) minx = x0;
            if (y0 < miny) miny = y0;
            if (x0 > maxx) maxx = x0;
            if (y0 > maxy) maxy = y0;
        }
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
    if (maxx >= minx)
        fb_mark((u16)minx, (u16)miny, (u16)(maxx + 1), (u16)(maxy + 1));
}

static void blit_rgb565(const u8 *img, u16 x0, u16 y0, u16 x1, u16 y1,
                        u16 img_w, u16 img_h)
{
    u16 x, y;
    const u8 *base = img + WP_HDR;

    if (x1 > LCD_W) x1 = LCD_W;
    if (y1 > LCD_H) y1 = LCD_H;
    if (y1 > img_h) y1 = img_h;
    if (x1 <= x0 || y1 <= y0)
        return;

    for (y = y0; y < y1; y++) {
        u16 *dst = lcd_fb + (u32)y * LCD_W + x0;
        const u8 *src = base + ((u32)y * img_w + x0) * 2u;
        for (x = 0; x < (u16)(x1 - x0); x++) {
            dst[x] = (u16)src[0] | ((u16)src[1] << 8);
            src += 2;
        }
    }
    fb_mark(x0, y0, x1, y1);
}

void lcd_draw_wallpaper(void)
{
    blit_rgb565(gImage_image3, 0, 0, LCD_W, LCD_H, WP_W, WP_HOME_H);
}

void lcd_restore_wallpaper(u16 x0, u16 y0, u16 x1, u16 y1)
{
    blit_rgb565(gImage_image3, x0, y0, x1, y1, WP_W, WP_HOME_H);
}

void lcd_draw_sub_wallpaper(void)
{
    blit_rgb565(gImage_image2, 0, 0, LCD_W, WP_SUB_H, WP_W, WP_SUB_H);
    if (LCD_H > WP_SUB_H)
        lcd_fill(0, WP_SUB_H, LCD_W, LCD_H, COLOR_WHITE);
}

void lcd_restore_sub_wallpaper(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u16 y_mid = y1;

    if (y0 < WP_SUB_H) {
        if (y_mid > WP_SUB_H)
            y_mid = WP_SUB_H;
        blit_rgb565(gImage_image2, x0, y0, x1, y_mid, WP_W, WP_SUB_H);
    }
    if (y1 > WP_SUB_H) {
        u16 py0 = (y0 > WP_SUB_H) ? y0 : WP_SUB_H;
        lcd_fill(x0, py0, x1, y1, COLOR_WHITE);
    }
}

void lcd_draw_cn16(u16 x, u16 y, const u8 *glyph, u16 fc, u16 bc)
{
    u16 row, col;

    if (x + 16 > LCD_W || y + 16 > LCD_H)
        return;

    for (row = 0; row < 16; row++) {
        u16 *dst = lcd_fb + (u32)(y + row) * LCD_W + x;
        for (col = 0; col < 16; col++) {
            u8 temp = glyph[col * 2 + (row >> 3)];
            if (temp & (u8)(0x01 << (row & 7)))
                dst[col] = fc;
            else if (bc != COLOR_TRANSPARENT)
                dst[col] = bc;
        }
    }
    fb_mark(x, y, x + 16, y + 16);
}

void lcd_draw_cn16_x2(u16 x, u16 y, const u8 *glyph, u16 fc, u16 bc)
{
    u16 row, col, r;

    if (x + 32 > LCD_W || y + 32 > LCD_H)
        return;

    for (row = 0; row < 16; row++) {
        for (r = 0; r < 2; r++) {
            u16 *dst = lcd_fb + (u32)(y + row * 2 + r) * LCD_W + x;
            for (col = 0; col < 16; col++) {
                u8 temp = glyph[col * 2 + (row >> 3)];
                if (temp & (u8)(0x01 << (row & 7))) {
                    dst[col * 2] = fc;
                    dst[col * 2 + 1] = fc;
                } else if (bc != COLOR_TRANSPARENT) {
                    dst[col * 2] = bc;
                    dst[col * 2 + 1] = bc;
                }
            }
        }
    }
    fb_mark(x, y, x + 32, y + 32);
}

void lcd_draw_cn16_x2_fg(u16 x, u16 y, const u8 *glyph, u16 fc)
{
    u16 row, col, r;

    if (x + 32 > LCD_W || y + 32 > LCD_H)
        return;

    for (row = 0; row < 16; row++) {
        for (r = 0; r < 2; r++) {
            u16 *dst = lcd_fb + (u32)(y + row * 2 + r) * LCD_W + x;
            for (col = 0; col < 16; col++) {
                u8 temp = glyph[col * 2 + (row >> 3)];
                if (temp & (u8)(0x01 << (row & 7))) {
                    dst[col * 2] = fc;
                    dst[col * 2 + 1] = fc;
                }
            }
        }
    }
    fb_mark(x, y, x + 32, y + 32);
}

void lcd_show_char(u16 x, u16 y, u8 num, u16 fc, u16 bc, u8 sizey)
{
    u8 temp, sizex, t, m = 0;
    u16 i, typeface, cx, cy;
    const u8 *font = 0;

    sizex = sizey / 2;
    num = (u8)(num - ' ');
    if (sizey == 16)
        font = ascii_1608[num];
    else if (sizey == 32)
        font = ascii_3216[num];
    else
        return;
    if (x + sizex > LCD_W || y + sizey > LCD_H)
        return;

    typeface = (u16)((sizex / 8 + ((sizex % 8) ? 1 : 0)) * sizey);
    cx = x;
    cy = y;
    m = 0;
    for (i = 0; i < typeface; i++) {
        temp = font[i];
        for (t = 0; t < 8; t++) {
            if (temp & (u8)(0x01 << t))
                lcd_fb[(u32)cy * LCD_W + cx] = fc;
            else if (bc != COLOR_TRANSPARENT)
                lcd_fb[(u32)cy * LCD_W + cx] = bc;
            cx++;
            m++;
            if (m == sizex) {
                m = 0;
                cx = x;
                cy++;
                break;
            }
        }
    }
    fb_mark(x, y, x + sizex, y + sizey);
}

void lcd_show_char_x2(u16 x, u16 y, u8 num, u16 fc, u16 bc, u8 sizey)
{
    u8 temp, sizex, t, m = 0;
    u16 i, typeface, r, cx, cy;
    u16 line[64];
    u8 on[64];
    const u8 *font = 0;

    sizex = sizey / 2;
    if (sizex > 64)
        return;
    num = (u8)(num - ' ');
    if (sizey == 16)
        font = ascii_1608[num];
    else if (sizey == 32)
        font = ascii_3216[num];
    else
        return;
    if (x + (u16)(sizex * 2) > LCD_W || y + (u16)(sizey * 2) > LCD_H)
        return;

    typeface = (u16)((sizex / 8 + ((sizex % 8) ? 1 : 0)) * sizey);
    cy = y;
    m = 0;
    for (i = 0; i < typeface; i++) {
        temp = font[i];
        for (t = 0; t < 8; t++) {
            on[m] = (temp & (u8)(0x01 << t)) ? 1 : 0;
            line[m] = on[m] ? fc : bc;
            m++;
            if (m == sizex) {
                for (r = 0; r < 2; r++) {
                    u16 k;
                    u16 *dst = lcd_fb + (u32)cy * LCD_W + x;
                    cx = 0;
                    for (k = 0; k < sizex; k++) {
                        if (on[k] || bc != COLOR_TRANSPARENT) {
                            dst[cx] = line[k];
                            dst[cx + 1] = line[k];
                        }
                        cx += 2;
                    }
                    cy++;
                }
                m = 0;
                break;
            }
        }
    }
    fb_mark(x, y, x + (u16)(sizex * 2), y + (u16)(sizey * 2));
}

void lcd_show_char_x2_fg(u16 x, u16 y, u8 num, u16 fc, u8 sizey)
{
    u8 temp, sizex, t, m = 0;
    u16 i, typeface, r, cx, cy;
    u8 on[64];
    const u8 *font = 0;

    sizex = sizey / 2;
    if (sizex > 64)
        return;
    num = (u8)(num - ' ');
    if (sizey == 16)
        font = ascii_1608[num];
    else if (sizey == 32)
        font = ascii_3216[num];
    else
        return;
    if (x + (u16)(sizex * 2) > LCD_W || y + (u16)(sizey * 2) > LCD_H)
        return;

    typeface = (u16)((sizex / 8 + ((sizex % 8) ? 1 : 0)) * sizey);
    cy = y;
    m = 0;
    for (i = 0; i < typeface; i++) {
        temp = font[i];
        for (t = 0; t < 8; t++) {
            on[m++] = (temp & (u8)(0x01 << t)) ? 1 : 0;
            if (m == sizex) {
                for (r = 0; r < 2; r++) {
                    u16 k;
                    u16 *dst = lcd_fb + (u32)cy * LCD_W + x;
                    cx = 0;
                    for (k = 0; k < sizex; k++) {
                        if (on[k]) {
                            dst[cx] = fc;
                            dst[cx + 1] = fc;
                        }
                        cx += 2;
                    }
                    cy++;
                }
                m = 0;
                break;
            }
        }
    }
    fb_mark(x, y, x + (u16)(sizex * 2), y + (u16)(sizey * 2));
}

void lcd_show_string(u16 x, u16 y, const char *s, u16 fc, u16 bc, u8 sizey)
{
    lcd_defer_start();
    while (*s) {
        lcd_show_char(x, y, (u8)*s, fc, bc, sizey);
        x += sizey / 2;
        s++;
    }
    lcd_defer_end();
}

void lcd_show_string_x2(u16 x, u16 y, const char *s, u16 fc, u16 bc, u8 sizey)
{
    lcd_defer_start();
    while (*s) {
        lcd_show_char_x2(x, y, (u8)*s, fc, bc, sizey);
        x += sizey;
        s++;
    }
    lcd_defer_end();
}

void lcd_show_int2(u16 x, u16 y, u8 v, u16 fc, u16 bc, u8 sizey)
{
    lcd_defer_start();
    lcd_show_char(x, y, (u8)('0' + (v / 10) % 10), fc, bc, sizey);
    lcd_show_char(x + sizey / 2, y, (u8)('0' + v % 10), fc, bc, sizey);
    lcd_defer_end();
}
