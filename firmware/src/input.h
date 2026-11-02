#ifndef INPUT_H
#define INPUT_H

#include "xil_types.h"

typedef enum {
    IN_NONE = 0,
    IN_N, IN_S, IN_W, IN_E, IN_C,
    IN_ENC_CW, IN_ENC_CCW, IN_ENC_PB,
    /* Touch: tap a row/button. Use touch_hit_consume_sel() for which one.
     * UI: if already selected -> activate (like IN_C); else just select. */
    IN_TAP
} input_evt_t;

void input_init(void);
/* Capture without calling UI/LCD; safe between LCD SPI blocks. */
void input_service(void);
int input_pending(void);
typedef struct {
    unsigned events, stale_events;
    unsigned max_poll_gap_us, max_dispatch_us;
} input_stats_t;
void input_get_stats(input_stats_t *stats);
/* Return one pending event (keys/encoder sticky), clears it. */
input_evt_t input_poll(void);

#endif
