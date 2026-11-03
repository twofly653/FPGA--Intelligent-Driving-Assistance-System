#ifndef VEML3328_H
#define VEML3328_H

#include "xil_types.h"

typedef struct {
    u16 clear;
    u16 r;
    u16 g;
    u16 b;
    u16 ir;
    u32 lux; /* G channel × 0.384 (IT=50ms, GAIN=1) */
} veml_sample_t;

int veml3328_init(void);
int veml3328_read(veml_sample_t *s);
/* Only updates Clear/Green/lux, preserves other cached channels.
 * Both reads leave the entire output unchanged if any I2C operation fails. */
int veml3328_read_lux_channels(veml_sample_t *s);

#endif
