#ifndef COLLISION_MODEL_H
#define COLLISION_MODEL_H

#include <stdint.h>

/* Bench-demo thresholds, in millimetres. Re-tune on the mounted vehicle. */
#define COLLISION_DANGER_MM       300
#define COLLISION_WARNING_MM      800
#define COLLISION_CLEAR_MARGIN_MM 100
#define COLLISION_STALE_MS        700u

typedef enum {
    COLLISION_UNKNOWN = 0,
    COLLISION_SAFE,
    COLLISION_WARNING,
    COLLISION_DANGER
} collision_level_t;

typedef struct {
    collision_level_t level;
    collision_level_t clear_candidate;
    uint8_t clear_count;
    uint8_t valid;
    int distance_mm;
    uint32_t last_valid_ms;
} collision_model_t;

void collision_model_init(collision_model_t *model);
void collision_model_sample(collision_model_t *model, int valid,
                            int distance_mm, uint32_t now_ms);
void collision_model_tick(collision_model_t *model, uint32_t now_ms);
const char *collision_level_name(collision_level_t level);

#endif
