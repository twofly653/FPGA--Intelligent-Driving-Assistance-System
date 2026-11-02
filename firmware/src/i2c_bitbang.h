#ifndef I2C_BITBANG_H
#define I2C_BITBANG_H

#include "xil_types.h"

void i2c_init(void);
int  i2c_probe(u8 addr7); /* 0 = ACK */
int  i2c_write(u8 addr7, const u8 *data, int n);
int  i2c_read(u8 addr7, u8 *data, int n);
int  i2c_write_read(u8 addr7, const u8 *wr, int nw, u8 *rd, int nr);
int  i2c_reg_write(u8 addr7, u8 reg, u8 val);
int  i2c_reg_read(u8 addr7, u8 reg, u8 *val);
int  i2c_reg_read_n(u8 addr7, u8 reg, u8 *buf, int n);

#endif
