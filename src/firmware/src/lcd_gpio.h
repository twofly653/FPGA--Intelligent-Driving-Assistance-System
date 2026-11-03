#ifndef LCD_GPIO_H
#define LCD_GPIO_H

#include "xil_types.h"

void lcd_gpio_init(void);
void lcd_gpio_set(u32 mask, u32 val);
void lcd_spi_write_byte(u8 dat);
/* RGB565, high byte first. CS/DC must already be set. */
void lcd_spi_burst_u16(const u16 *data, u32 count);

#endif
