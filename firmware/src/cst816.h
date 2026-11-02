#ifndef CST816_H
#define CST816_H

#include "xil_types.h"

#define CST816_ADDR7  0x15u

int  cst816_init(void);          /* 0=ok; -1=no ACK (keys still work) */
int  cst816_ok(void);
/* While finger down: returns 1 with coords until cst816_mark_handled(). */
int  cst816_poll_press(u16 *x, u16 *y);
void cst816_mark_handled(void);
void cst816_stats(unsigned *reads, unsigned *errors);

#endif
