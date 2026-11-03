#ifndef HDC1080_H
#define HDC1080_H

#include "xil_types.h"

int hdc1080_init(void);

/* Blocking read (starts conversion + waits). Prefer async API in UI loops. */
int hdc1080_read(int *temp_c10, int *rh_pct10);

/* Non-blocking: start a conversion (call once). */
int hdc1080_start(void);
/* Non-blocking: fetch result after ~15 ms; returns -1 if not ready. */
int hdc1080_fetch(int *temp_c10, int *rh_pct10);

#endif
