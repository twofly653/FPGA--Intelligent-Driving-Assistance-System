#ifndef TRAFFIC_SERVICE_H
#define TRAFFIC_SERVICE_H
#include <stdint.h>
typedef struct {
    uint32_t seq, age_ms, proc_ms, capture_ms, copy_ms;
    unsigned color,valid,confidence,digit_valid;
    int countdown;
    /* Low-rate engineering counters; not interpreted as a valid countdown. */
    unsigned diagnostic_candidates,diagnostic_rejected,diagnostic_conflicts,diagnostic_streak;
    int diagnostic_digit;
} traffic_snapshot_t;
typedef struct {
    const uint16_t *pixels;
    unsigned width,height;
    uint32_t source_frame,sequence;
    uint64_t capture_us;
} traffic_frame_snapshot_t;
/* Main-loop immutable view, consumed at most once. Pointer is valid only until
 * the NEXT traffic_service_poll(). Never retain it or use from an ISR. */
int traffic_service_take_frame(traffic_frame_snapshot_t *out);
/* Non-consuming view for the Ethernet sender; same lifetime as take_frame. */
int traffic_service_peek_frame(traffic_frame_snapshot_t *out);
void traffic_service_init(void);
/* Main-loop only: bounded thumbnail reads; never called recursively by LCD. */
void traffic_service_poll(void);
/* Clear history/HUD BEFORE a blocking JPEG capture can stop polling. */
void traffic_service_suspend(void);
void traffic_service_get_snapshot(traffic_snapshot_t *out);
#endif
