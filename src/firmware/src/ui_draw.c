#include "ui_draw.h"
#include "ui_font_data.h"
#include <string.h>
#include <math.h>
#ifdef UI_HOST_PREVIEW
#include <stdio.h>
unsigned ui_host_text_overflows;
#endif

static const ui_font_data_t *font_data(ui_font_t font)
{
    unsigned index=(unsigned)font;
    return &ui_fonts[index<5u?index:1u];
}
unsigned ui_text_width(const char *text,ui_font_t font)
{
    const ui_font_data_t *f=font_data(font);
    unsigned n=0;
    if(!text)return 0;
    while(*text){unsigned ch=(unsigned char)*text++;if(ch<32||ch>126)ch='?';n+=f->width[ch-32];}
    return n;
}
void ui_text(u16 x,u16 y,const char *text,ui_font_t font,u16 fg,u16 bg)
{
    const ui_font_data_t *f=font_data(font);
    if(!text)return;
#ifdef UI_HOST_PREVIEW
    if((unsigned)x+ui_text_width(text,font)>LCD_W || (unsigned)y+f->height>LCD_H){
        ++ui_host_text_overflows;fprintf(stderr,"TEXT_OVERFLOW %u,%u %s\n",x,y,text);
    }
#endif
    while(*text){
        unsigned ch=(unsigned char)*text++,w;
        if(ch<32||ch>126) ch='?';
        ch-=32;
        w=f->width[ch];
        if((unsigned)x+w>LCD_W || (unsigned)y+f->height>LCD_H)break;
        lcd_blend_mask(x,y,(u16)w,(u16)f->height,f->pixels+f->offset[ch],fg,bg);
        x=(u16)(x+w);
    }
}
void ui_text_center(u16 x,u16 y,u16 w,const char *text,ui_font_t font,u16 fg,u16 bg)
{
    unsigned tw=ui_text_width(text,font);
    ui_text((u16)(x+(tw<w?(w-tw)/2u:0u)),y,text,font,fg,bg);
}
static void rounded(u16 x,u16 y,u16 w,u16 h,u16 color)
{
    unsigned i,r=8;
    if(w<16||h<16)r=3;
    lcd_fill((u16)(x+r),y,(u16)(x+w-r),(u16)(y+h),color);
    lcd_fill(x,(u16)(y+r),(u16)(x+w),(u16)(y+h-r),color);
    for(i=0;i<r;i++){
        unsigned inset=r-(unsigned)sqrtf((float)(r*r-(r-i-1)*(r-i-1)));
        lcd_fill((u16)(x+inset),(u16)(y+i),(u16)(x+w-inset),(u16)(y+i+1),color);
        lcd_fill((u16)(x+inset),(u16)(y+h-i-1),(u16)(x+w-inset),(u16)(y+h-i),color);
    }
}
void ui_panel(u16 x,u16 y,u16 w,u16 h,u16 accent)
{
    rounded(x,y,w,h,accent?accent:COLOR_DASH_BORDER);
    if(w>2&&h>2)rounded((u16)(x+1),(u16)(y+1),(u16)(w-2),(u16)(h-2),COLOR_DASH_CARD);
}
static void ring(int x,int y,int r,u16 color)
{
    int a=-r,b=0,e=2-2*r;
    do { lcd_fill((u16)(x-a),(u16)(y+b),(u16)(x-a+1),(u16)(y+b+1),color);
         lcd_fill((u16)(x-b),(u16)(y-a),(u16)(x-b+1),(u16)(y-a+1),color);
         lcd_fill((u16)(x+a),(u16)(y-b),(u16)(x+a+1),(u16)(y-b+1),color);
         lcd_fill((u16)(x+b),(u16)(y+a),(u16)(x+b+1),(u16)(y+a+1),color);
         r=e;if(r<=b)e+=++b*2+1;if(r>a||e>b)e+=++a*2+1;
    }while(a<0);
}
void ui_icon(u16 x,u16 y,unsigned kind,u16 color)
{
    int a=x,b=y,i;
    switch(kind){
    case 0: for(i=0;i<3;i++){lcd_line(a+2,b+4+i*6,a+18,b+4+i*6,color);lcd_fill((u16)(a+5+i*4),(u16)(b+2+i*6),(u16)(a+8+i*4),(u16)(b+7+i*6),color);} break;
    case 1: ring(a+10,b+10,9,color);lcd_line(a+13,b+4,a+7,b+16,color);lcd_line(a+13,b+4,a+14,b+11,color);lcd_line(a+7,b+16,a+6,b+9,color);break;
    case 2: ring(a+10,b+10,5,color);for(i=0;i<8;i++){float k=(float)i*0.78539816f;lcd_line(a+10+(int)(cosf(k)*8),b+10+(int)(sinf(k)*8),a+10+(int)(cosf(k)*10),b+10+(int)(sinf(k)*10),color);}break;
    case 3: ring(a+10,b+10,9,color);lcd_line(a+3,b+12,a+17,b+8,color);lcd_line(a+7,b+10,a+13,b+10,color);lcd_line(a+10,b+7,a+10,b+13,color);break;
    case 4: lcd_line(a+5,b+15,a+5,b+2,color);lcd_line(a+5,b+15,a+19,b+15,color);lcd_line(a+5,b+15,a+14,b+6,color);break;
    case 5: for(i=0;i<5;i++)lcd_fill((u16)(a+2+i*4),(u16)(b+8-(i%3)*3),(u16)(a+4+i*4),(u16)(b+13+(i%3)*3),color);break;
    case 6: ring(a+10,b+8,7,color);lcd_line(a+6,b+15,a+14,b+15,color);lcd_line(a+7,b+18,a+13,b+18,color);break;
    case 7: lcd_line(a+1,b+4,a+1,b+17,color);lcd_line(a+19,b+4,a+19,b+17,color);lcd_line(a+3,b+10,a+17,b+10,color);lcd_line(a+3,b+10,a+7,b+6,color);lcd_line(a+3,b+10,a+7,b+14,color);lcd_line(a+17,b+10,a+13,b+6,color);lcd_line(a+17,b+10,a+13,b+14,color);break;
    case 8: for(i=0;i<2;i++){int xx=a+2+i*16;lcd_line(xx,b+2,xx+(i?-5:5),b+2,color);lcd_line(xx,b+2,xx,b+7,color);lcd_line(xx,b+18,xx+(i?-5:5),b+18,color);lcd_line(xx,b+18,xx,b+13,color);}lcd_line(a+7,b+7,a+13,b+7,color);lcd_line(a+13,b+7,a+13,b+13,color);lcd_line(a+13,b+13,a+7,b+13,color);lcd_line(a+7,b+13,a+7,b+7,color);break;
    default: for(i=0;i<4;i++)rounded((u16)(a+1+(i%2)*11),(u16)(b+1+(i/2)*11),8,8,color);break;
    }
}
void ui_draw_tile(u16 x,u16 y,u16 side,const char *line1,const char *line2,int selected)
{
    ui_font_t font=side<=52?UI_FONT_SMALL:UI_FONT_BODY;
    u16 fg=COLOR_DASH_TEXT;
    ui_panel(x,y,side,side,selected?COLOR_DASH_CYAN:COLOR_DASH_BORDER);
    if(selected)lcd_fill((u16)(x+side/2-9),(u16)(y+3),(u16)(x+side/2+9),(u16)(y+5),COLOR_DASH_CYAN);
    if(ui_text_width(line1,font)>side-10u)font=UI_FONT_SMALL;
    if(line1 && (!strcmp(line1,"<") || !strcmp(line1,">"))){
        int cx=x+side/2,cy=y+15,dir=!strcmp(line1,"<")?-1:1;
        lcd_line(cx-dir*4,cy-5,cx+dir*3,cy,COLOR_DASH_CYAN);
        lcd_line(cx+dir*3,cy,cx-dir*4,cy+5,COLOR_DASH_CYAN);
        if(line2)ui_text_center(x,(u16)(y+side-18),side,line2,UI_FONT_SMALL,COLOR_DASH_MUTED,COLOR_DASH_CARD);
    }else{
        ui_text_center(x,(u16)(y+side/2-(line2?17:8)),side,line1,font,fg,COLOR_DASH_CARD);
        if(line2)ui_text_center(x,(u16)(y+side/2+5),side,line2,UI_FONT_SMALL,selected?COLOR_DASH_CYAN:COLOR_DASH_MUTED,COLOR_DASH_CARD);
    }
}
void ui_draw_back_tile(void){ui_draw_tile(8,8,48,"<","BACK",0);}
void ui_header(const char *title,const char *subtitle)
{
    ui_font_t f=ui_text_width(title,UI_FONT_TITLE)>108?UI_FONT_BODY:UI_FONT_TITLE;
    lcd_fill(0,0,LCD_W,LCD_H,COLOR_DASH_BG);
    ui_draw_back_tile();
    ui_text(68,13,title,f,COLOR_DASH_TEXT,COLOR_DASH_BG);
    if(subtitle)ui_text(68,37,subtitle,UI_FONT_SMALL,COLOR_DASH_MUTED,COLOR_DASH_BG);
    lcd_line(12,62,228,62,COLOR_DASH_BORDER);
}
void ui_footer(const char *text)
{
    lcd_fill(8,263,232,284,COLOR_DASH_BG);
    ui_text_center(8,267,224,text,UI_FONT_SMALL,COLOR_DASH_MUTED,COLOR_DASH_BG);
}
void ui_theme_colors(int selected,u16 *fc,u16 *bc){*fc=selected?COLOR_DASH_CYAN:COLOR_DASH_TEXT;*bc=COLOR_DASH_CARD;}
void ui_draw_row_box(u16 x,u16 y,u16 w,u16 h,int selected){ui_panel(x,y,w,h,selected?COLOR_DASH_CYAN:COLOR_DASH_BORDER);}
void ui_draw_back_row(u16 y,int selected){ui_draw_tile(8,y,48,"<","BACK",selected);}
