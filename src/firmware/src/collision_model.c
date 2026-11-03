#include "collision_model.h"

static collision_level_t raw_level(int distance_mm)
{
    if (distance_mm <= COLLISION_DANGER_MM)
        return COLLISION_DANGER;
    if (distance_mm <= COLLISION_WARNING_MM)
        return COLLISION_WARNING;
    return COLLISION_SAFE;
}

void collision_model_init(collision_model_t *model)
{
    if (!model)
        return;
    model->level = COLLISION_UNKNOWN;
    model->clear_candidate = COLLISION_UNKNOWN;
    model->clear_count = 0;
    model->valid = 0;
    model->distance_mm = 0;
    model->last_valid_ms = 0;
}

void collision_model_sample(collision_model_t *model, int valid,
                            int distance_mm, uint32_t now_ms)
{
    collision_level_t raw;
    collision_level_t candidate;
    if (!model)
        return;
    /* A failed or out-of-range reading must never preserve a SAFE result. */
    if (!valid || distance_mm < 0 || distance_mm > 4000) {
        model->valid = 0;
        model->distance_mm = 0;
        model->level = COLLISION_UNKNOWN;
        model->clear_candidate = COLLISION_UNKNOWN;
        model->clear_count = 0;
        return;
    }
    model->valid = 1;
    model->distance_mm = distance_mm;
    model->last_valid_ms = now_ms;
    raw = raw_level(distance_mm);
    if (model->level == COLLISION_UNKNOWN || raw > model->level) {
        /* Escalate immediately, so a near obstacle is not held by filtering. */
        model->level = raw;
        model->clear_candidate = COLLISION_UNKNOWN;
        model->clear_count = 0;
        return;
    }
    if (raw == model->level) {
        model->clear_candidate = COLLISION_UNKNOWN;
        model->clear_count = 0;
        return;
    }
    /* Require both a margin and three consecutive valid readings to clear. */
    candidate = raw;
    if (model->level == COLLISION_DANGER) {
        if (distance_mm <= COLLISION_DANGER_MM + COLLISION_CLEAR_MARGIN_MM)
            candidate = COLLISION_DANGER;
        else
            candidate = COLLISION_WARNING; /* Clear one severity step at a time. */
    }
    if (model->level == COLLISION_WARNING &&
        distance_mm <= COLLISION_WARNING_MM + COLLISION_CLEAR_MARGIN_MM)
        candidate = COLLISION_WARNING;
    if (candidate == model->level) {
        model->clear_candidate = COLLISION_UNKNOWN;
        model->clear_count = 0;
        return;
    }
    if (model->clear_candidate != candidate) {
        model->clear_candidate = candidate;
        model->clear_count = 1;
    } else if (model->clear_count < 3) {
        model->clear_count++;
    }
    if (model->clear_count >= 3) {
        model->level = candidate;
        model->clear_candidate = COLLISION_UNKNOWN;
        model->clear_count = 0;
    }
}

void collision_model_tick(collision_model_t *model, uint32_t now_ms)
{
    if (!model || !model->valid)
        return;
    if ((uint32_t)(now_ms - model->last_valid_ms) > COLLISION_STALE_MS)
        collision_model_sample(model, 0, 0, now_ms);
}

const char *collision_level_name(collision_level_t level)
{
    switch (level) {
    case COLLISION_SAFE: return "SAFE";
    case COLLISION_WARNING: return "WARNING";
    case COLLISION_DANGER: return "DANGER";
    default: return "NO DATA";
    }
}
