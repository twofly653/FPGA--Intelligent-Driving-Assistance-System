#ifndef OBSTACLE_SERVICE_H
#define OBSTACLE_SERVICE_H
#include <stdint.h>
#include "pc_net_protocol.h"
typedef struct {
 uint32_t sequence,source_frame,age_ms,proc_ms,processed,commits,busy_deferred;
 unsigned enabled,available,status,count;
} obstacle_service_snapshot_t;
typedef struct {uint16_t id,xmin,ymin,xmax,ymax;} obstacle_display_box_t;
typedef struct {
 uint32_t sequence,source_frame,age_ms,proc_ms;
 unsigned available,status,count;
 obstacle_display_box_t boxes[8];
} obstacle_display_snapshot_t;
/* Only a mailbox already ACKed by the pixel domain may be exported to PC. */
void obstacle_service_get_display_snapshot(obstacle_display_snapshot_t *out);
/* Main-loop control; explicit, idempotent user preference, independent of JPEG pause. */
void obstacle_service_set_enabled(unsigned enabled);
unsigned obstacle_service_is_enabled(void);
unsigned obstacle_service_is_paused(void);
void obstacle_service_remote_clear(void);
void obstacle_service_remote_apply(const pn_box_t *boxes,unsigned count,uint32_t frame,uint64_t capture_us,unsigned proc_ms);
void obstacle_service_init(void);
#if defined(OBSTACLE_CAPTURE_DEBUG)
void obstacle_service_debug_begin(void);
#endif
/* Main-loop only, immediately consumes one immutable shared RGB frame.
 * No sensor I/O, waits, camera rearming, or full-frame copies. */
void obstacle_service_poll(void);
/* Request asynchronous revocation and pause analysis before blocking JPEG. */
void obstacle_service_suspend(void);
/* Poll from the main loop; ready only after clear was accepted and ACKed. */
int obstacle_service_suspend_ready(void);
void obstacle_service_resume(void);
void obstacle_service_get_snapshot(obstacle_service_snapshot_t *out);
#endif
