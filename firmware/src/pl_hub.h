/* pl_hub.h - AXI-Lite register map for axi_pl_hub */

#ifndef PL_HUB_H
#define PL_HUB_H

#include "xil_io.h"
#include "xparameters.h"

#ifdef XPAR_AXI_PL_HUB_0_S00_AXI_BASEADDR
#define PL_HUB_BASE   XPAR_AXI_PL_HUB_0_S00_AXI_BASEADDR
#elif defined(XPAR_AXI_PL_HUB_0_BASEADDR)
#define PL_HUB_BASE   XPAR_AXI_PL_HUB_0_BASEADDR
#else
/* The integrated BD explicitly maps the Hub to 0x40000000..0x4000FFFF.
 * The archived BSP has no Hub macro, so use the verified release address. */
#define PL_HUB_BASE   0x40000000U
#endif

#if PL_HUB_BASE != 0x40000000U
#error "PL Hub address differs from the integrated hardware release"
#endif

#define REG_STATUS      0x00
#define REG_STATUS_CLR  0x04
#define REG_SEG_BRIGHT  0x08
#define REG_SEG_BLANK   0x0C
#define REG_SEG_HEX0    0x10
#define REG_SEG_HEX1    0x14
#define REG_BUZ_CTRL    0x18
#define REG_BUZ_FREQ    0x1C
#define REG_BUZ_MS      0x20
#define REG_WS_CTRL     0x24
#define REG_WS_COLOR    0x28
#define REG_WS_BRIGHT   0x2C
#define REG_LCD_GPIO    0x30
#define REG_KEY_STAT    0x34
#define REG_ENC_STAT    0x38
#define REG_INPUT_CLR   0x3C
#define REG_I2C_GPIO    0x40
#define REG_MIC_STAT    0x44
#define REG_TP_GPIO     0x48
#define REG_LCD_TX      0x4C
#define REG_LCD_TX_STAT 0x50
#define REG_DEBUG_LED   0x54
#define LCD_TX_MAGIC    0x4C530000u
#define LCD_TX_BUSY     1u
#define LCD_TX_FULL     2u

/* STATUS 0x00: [3:0] level  [7:4] sticky  [8]buz [9]ws */
#define ST_BTN_LEVEL(i)   (1U << (i))
#define ST_BTN_STICKY(i)  (1U << (4 + (i)))
#define ST_BUZ_BUSY       (1U << 8)
#define ST_WS_BUSY        (1U << 9)

/* KEY_STAT: level [4:0]=N,S,W,E,C  sticky [12:8] */
#define KEY_N   (1U << 0)
#define KEY_S   (1U << 1)
#define KEY_W   (1U << 2)
#define KEY_E   (1U << 3)
#define KEY_C   (1U << 4)

/* ENC_STAT */
#define ENC_ST_CW   (1U << 8)
#define ENC_ST_CCW  (1U << 9)
#define ENC_ST_PB   (1U << 10)

/* WS2812 GRB */
#define WS_RED     0x00FF00u
#define WS_GREEN   0xFF0000u
#define WS_BLUE    0x0000FFu
#define WS_YELLOW  0xFFFF00u /* G+R */
#define WS_PURPLE  0x00FFFFu /* R+B */

#define WS_MODE_OFF      0
#define WS_MODE_FLOW     1
#define WS_MODE_ALLON    2
#define WS_MODE_BLINK    3
#define WS_MODE_RAINBOW  4

#define LCD_GPIO_SCK    (1U << 0)
#define LCD_GPIO_MOSI   (1U << 1)
#define LCD_GPIO_DC     (1U << 2)
#define LCD_GPIO_CS     (1U << 3)
#define LCD_GPIO_RST    (1U << 4)
#define LCD_GPIO_BLK    (1U << 5)

static inline u32  pl_rd(u32 off) { return Xil_In32(PL_HUB_BASE + off); }
static inline void pl_wr(u32 off, u32 v) { Xil_Out32(PL_HUB_BASE + off, v); }

u32  pl_status(void);
int  pl_btn_pressed(int i);      /* 0..3 edge sticky */
void pl_btn_clear(int i);
int  pl_btn0_pressed(void);
void pl_btn0_clear(void);

void seg_set_bright(u8 pct);
void seg_blank_all(void);
void seg_show_boot(void);
void seg_show_time(u8 letter_hex, u8 hh, u8 mm, u8 ss);
/* Nine physical digits L→R; blank_mask bit0 blanks the first digit. */
void seg_show_digits(const u8 digits[9], u16 blank_mask);

void buz_beep(u16 hz, u16 ms);

void ws_set_color(u32 grb);
void ws_set_bright(u8 pct);
void ws_start(u32 mode);
u32  ws_last_mode(void);
void ws_wait_idle(void);

void lcd_gpio_write(u32 val);
u32  lcd_gpio_read(void);

void i2c_gpio_write(u32 val);
u32  i2c_gpio_read(void);

u8   mic_db(void);
u16  mic_amp(void);

u32  pl_key_stat(void);
u32  pl_enc_stat(void);
void pl_key_clear(u32 mask_nswec);   /* bits 0..4 */
void pl_enc_clear(u32 mask);         /* ENC_ST_* */

/* Touch RST/INT: REG_TP_GPIO bit0=rst, bit8=int level */
void pl_tp_rst(int high);
int  pl_tp_int_level(void);

#endif
