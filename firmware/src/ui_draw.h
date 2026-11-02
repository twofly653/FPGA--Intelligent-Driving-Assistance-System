#ifndef UI_DRAW_H
#define UI_DRAW_H
#include "xil_types.h"
#include "lcd_st7789.h"
typedef enum { UI_FONT_SMALL=0, UI_FONT_BODY, UI_FONT_TITLE, UI_FONT_VALUE, UI_FONT_TIMER } ui_font_t;
unsigned ui_text_width(const char *text, ui_font_t font);
void ui_text(u16 x,u16 y,const char *text,ui_font_t font,u16 fg,u16 bg);
void ui_text_center(u16 x,u16 y,u16 w,const char *text,ui_font_t font,u16 fg,u16 bg);
void ui_panel(u16 x,u16 y,u16 w,u16 h,u16 accent);
void ui_header(const char *title,const char *subtitle);
void ui_footer(const char *text);
void ui_icon(u16 x,u16 y,unsigned kind,u16 color);
void ui_draw_tile(u16 x,u16 y,u16 side,const char *line1,const char *line2,int selected);
void ui_draw_back_tile(void);
void ui_theme_colors(int selected,u16 *fc,u16 *bc);
void ui_draw_row_box(u16 x,u16 y,u16 w,u16 h,int selected);
void ui_draw_back_row(u16 y,int selected);
#ifdef UI_HOST_PREVIEW
extern unsigned ui_host_text_overflows;
#endif
#endif
