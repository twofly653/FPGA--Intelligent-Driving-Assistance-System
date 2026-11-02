#ifndef HARDWARE_SYNC_PROTOCOL_H
#define HARDWARE_SYNC_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include "obstacle_service.h"
typedef struct {uint32_t sequence;unsigned mode,live,configured,width,height;} camera_sync_snapshot_t;
/* Fair round-robin among seven due telemetry streams; -1 if none. */
int hardware_sync_next_due(unsigned mask,unsigned *cursor);
int hardware_sync_detection(char *out,size_t size,uint32_t sequence,unsigned enabled);
int hardware_sync_camera(char *out,size_t size,const camera_sync_snapshot_t *v);
int hardware_sync_obstacles(char *out,size_t size,const obstacle_display_snapshot_t *v);
#endif
