#ifndef HOST_XIL_IO_H
#define HOST_XIL_IO_H
#include "xil_types.h"
static inline u32 Xil_In32(UINTPTR a){(void)a;return 0;}
static inline void Xil_Out32(UINTPTR a,u32 v){(void)a;(void)v;}
#endif
