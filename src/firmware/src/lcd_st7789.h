#ifndef LCD_ST7789_H
#define LCD_ST7789_H

#include "xil_types.h"

#define LCD_W 240
/* Panel visible height is 284 (P183B001 driver). */
#define LCD_H 284

#define COLOR_WHITE   0xFFFF
#define COLOR_BLACK   0x0000
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_PURPLE  0xF81F
/* Contest menu theme (wallpaper pages) */
#define COLOR_LT_PURPLE  0xC55C  /* 淡紫选中底 */
#define COLOR_LT_BLUE    0x19B8  /* 子页未选中：深蓝冷色，白底可读 */
#define COLOR_SEL_FG     0xFFFF  /* 非首页选中字：白字配淡紫底 */
/* Dashboard palette used by compass / distance pages (ported from teammate) */
#define COLOR_DASH_BG 0x08A4
#define COLOR_DASH_CARD 0x1126
#define COLOR_DASH_CYAN 0x4F19
#define COLOR_DASH_AMBER 0xFDED
#define COLOR_DASH_MUTED 0x9577
/* 背景透传：不写像素，留给壁纸 */
#define COLOR_TRANSPARENT 0x0001

#define COLOR_DASH_BORDER 0x2A0A

#define COLOR_DASH_TEXT 0xEFBF

#define COLOR_DASH_GREEN 0x6ED4

#define COLOR_DASH_RED 0xFB90

void lcd_blend_mask(u16 x, u16 y, u16 w, u16 h, const u8 *mask, u16 fg, u16 bg);
#ifdef UI_HOST_PREVIEW
const u16 *lcd_host_framebuffer(void);
#endif

void lcd_init(void);
/* Optional cooperative sensor poll between pixel strips. The callback must
 * not call LCD functions or alter LCD GPIO; I2C uses a separate PL register.
 */
void lcd_set_idle_hook(void (*hook)(void));
/* Bounded cooperative display work; call frequently from the main loop.
 * Framebuffer drawing only queues 16x8 dirty blocks and never waits for SPI.
 * The 2 ms budget is checked between blocks, including the idle callback. */
void lcd_poll(void);
void lcd_get_stats(unsigned *pending, unsigned *max_slice_us);

/*
 * 先画到 DDR 帧缓冲，再由 lcd_poll 分块送屏。
 * lcd_defer_start/end 可嵌套：绘图事务未结束时禁止发送未完成帧。
 */
void lcd_defer_start(void);
void lcd_defer_end(void);

void lcd_fill(u16 x0, u16 y0, u16 x1, u16 y1, u16 color);
void lcd_line(int x0, int y0, int x1, int y1, u16 color);
void lcd_draw_wallpaper(void);
void lcd_restore_wallpaper(u16 x0, u16 y0, u16 x1, u16 y1);
void lcd_draw_sub_wallpaper(void);
void lcd_restore_sub_wallpaper(u16 x0, u16 y0, u16 x1, u16 y1);
void lcd_draw_cn16(u16 x, u16 y, const u8 *glyph, u16 fc, u16 bc);
void lcd_draw_cn16_x2(u16 x, u16 y, const u8 *glyph, u16 fc, u16 bc);
/* 只画笔画（壁纸上叠白字用） */
void lcd_draw_cn16_x2_fg(u16 x, u16 y, const u8 *glyph, u16 fc);
void lcd_show_char(u16 x, u16 y, u8 num, u16 fc, u16 bc, u8 sizey);
void lcd_show_char_x2(u16 x, u16 y, u8 num, u16 fc, u16 bc, u8 sizey);
void lcd_show_char_x2_fg(u16 x, u16 y, u8 num, u16 fc, u8 sizey);
void lcd_show_string(u16 x, u16 y, const char *s, u16 fc, u16 bc, u8 sizey);
void lcd_show_string_x2(u16 x, u16 y, const char *s, u16 fc, u16 bc, u8 sizey);
void lcd_show_int2(u16 x, u16 y, u8 v, u16 fc, u16 bc, u8 sizey);

#endif
