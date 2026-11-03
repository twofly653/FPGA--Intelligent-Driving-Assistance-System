#include "lcd_gpio.h"
#include "pl_hub.h"

static u32 g_lcd;
static int g_hardware_spi;

static void lcd_spi_drain(void)
{
    if (g_hardware_spi)
        while (pl_rd(REG_LCD_TX_STAT) & LCD_TX_BUSY) { }
}

#if defined(__GNUC__)
#define LCD_SPI_HOT __attribute__((optimize("O2"), noinline))
#define LCD_SPI_INLINE static inline __attribute__((always_inline, optimize("O2")))
#else
#define LCD_SPI_HOT
#define LCD_SPI_INLINE static inline
#endif

#define LCD_GPIO_REG (*(volatile u32 *)(PL_HUB_BASE + REG_LCD_GPIO))

void lcd_gpio_init(void)
{
    /* CS/RST/BLK high, SCK/MOSI/DC low — same as project_6 (fast soft-SPI) */
    g_lcd = LCD_GPIO_CS | LCD_GPIO_RST | LCD_GPIO_BLK;
    lcd_gpio_write(g_lcd);
    g_hardware_spi = (pl_rd(REG_LCD_TX_STAT) & 0xFFFF0000u) == LCD_TX_MAGIC;
    if (g_hardware_spi) pl_wr(REG_DEBUG_LED, 0);
}

void lcd_gpio_set(u32 mask, u32 val)
{
    lcd_spi_drain();
    g_lcd = (g_lcd & ~mask) | (val & mask);
    lcd_gpio_write(g_lcd);
}

LCD_SPI_INLINE void spi_byte_mode0(u32 base, u8 dat)
{
    u32 v;
    /* 8 bits, MOSI setup while SCK low, sample on rising edge.
     * No AXI settle reads — those made every bit ~5x slower than project_6. */
#define SPI_BIT(i) \
    do { \
        v = base | ((((u32)(dat) >> (i)) & 1u) * LCD_GPIO_MOSI); \
        LCD_GPIO_REG = v; \
        LCD_GPIO_REG = v | LCD_GPIO_SCK; \
    } while (0)

    SPI_BIT(7);
    SPI_BIT(6);
    SPI_BIT(5);
    SPI_BIT(4);
    SPI_BIT(3);
    SPI_BIT(2);
    SPI_BIT(1);
    SPI_BIT(0);
#undef SPI_BIT
}

void lcd_spi_write_byte(u8 dat)
{
    u32 base = g_lcd & ~(LCD_GPIO_SCK | LCD_GPIO_MOSI);
    lcd_spi_drain();

    spi_byte_mode0(base, dat);
    g_lcd = base;
    LCD_GPIO_REG = g_lcd;
}

LCD_SPI_HOT void lcd_spi_burst_u16(const u16 *data, u32 count)
{
    u32 base = g_lcd & ~(LCD_GPIO_SCK | LCD_GPIO_MOSI);

    if (g_hardware_spi) {
        /* Commands still use GPIO mode; RGB565 pixels use the PL FIFO.
         * A tile is bounded at 128 pixels, then main-loop input can run. */
        g_lcd = base;
        LCD_GPIO_REG = base;
        while (count) {
            u32 free_words = (pl_rd(REG_LCD_TX_STAT) >> 8) & 0xFFu;
            if (free_words > count) free_words = count;
            count -= free_words;
            while (free_words--) pl_wr(REG_LCD_TX, *data++);
        }
        lcd_spi_drain(); /* Last falling edge must finish before CS/DC changes. */
        return;
    }

    while (count--) {
        u16 p = *data++;
        spi_byte_mode0(base, (u8)(p >> 8));
        spi_byte_mode0(base, (u8)(p & 0xFF));
    }
    g_lcd = base;
    LCD_GPIO_REG = g_lcd;
}
