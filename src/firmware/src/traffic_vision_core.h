#ifndef TRAFFIC_VISION_CORE_H
#define TRAFFIC_VISION_CORE_H

#include <stdint.h>

#define TRAFFIC_VISION_MAX_WIDTH 320u
#define TRAFFIC_VISION_MAX_HEIGHT 240u
#define TRAFFIC_VISION_MAX_PIXELS (TRAFFIC_VISION_MAX_WIDTH*TRAFFIC_VISION_MAX_HEIGHT)
enum { TRAFFIC_UNKNOWN=0, TRAFFIC_RED=1, TRAFFIC_AMBER=2, TRAFFIC_GREEN=3 };

typedef struct { uint16_t x,y,w,h; } traffic_vision_bbox_t;
typedef struct {
    uint32_t frame_number;
    uint32_t classified_pixels;
    uint32_t components;
    uint32_t lamp_candidates;
    uint32_t rejected_shape;
    uint32_t rejected_background;
    uint32_t digit_candidates;
    uint32_t digit_rejected;
    uint32_t conflicts;
} traffic_vision_diagnostics_t;
typedef struct {
    uint8_t color;                 /* 0unknown/1red/2amber/3green */
    uint8_t valid;                 /* temporally confirmed single lamp */
    uint8_t confidence;            /* rule confidence0..100, NOT accuracy */
    uint8_t countdown_valid;
    uint8_t countdown_confidence;
    int16_t countdown;             /* -1unknown, otherwise0..99; never estimated */
    traffic_vision_bbox_t lamp_bbox;
    traffic_vision_bbox_t digit_bbox;
    traffic_vision_diagnostics_t diagnostics;
} traffic_vision_result_t;

/* Allocate statically/in DDR, not on the small bare-metal application stack.
 * No malloc, floating point, board I/O, telemetry or display calls are used. */
typedef struct {
    traffic_vision_bbox_t roi;     /* w/h zero => full input; default full frame */
    uint8_t color_confirm_frames,digit_confirm_frames,min_confidence;
    uint8_t min_lamp_diameter,max_lamp_diameter;
    uint8_t candidate_color,color_streak;
    int16_t candidate_digit;
    uint8_t digit_streak,digit_misses;
    traffic_vision_bbox_t candidate_lamp; /* temporal association; resets on target swap */
    uint32_t frame_number;
    uint8_t mask[TRAFFIC_VISION_MAX_PIXELS];
    union {
        uint32_t queue[TRAFFIC_VISION_MAX_PIXELS];
        uint16_t ocr_tile[TRAFFIC_VISION_MAX_PIXELS*2u]; /* same storage, after component analysis */
    };
    uint16_t projection_y[TRAFFIC_VISION_MAX_HEIGHT];
    uint16_t projection_x[TRAFFIC_VISION_MAX_WIDTH];
} traffic_vision_t;

void traffic_vision_init(traffic_vision_t *state);
void traffic_vision_reset(traffic_vision_t *state); /* reset history; retain ROI/config */
void traffic_vision_set_roi(traffic_vision_t *state,unsigned x,unsigned y,unsigned w,unsigned h);
/* Frame is immutable original-colour RGB565, row-major, no stride/padding.
 * Returns0 for a processed frame (including unknown), -1 for bad/missing input.
 * NULL/missing input clears history AND emits unknown, so a later frame cannot
 * inherit stale colour/countdown. Caller must likewise invalidate on source age.
 * Seven-segment OCR supports bright1/2-digit displays about12..56px high near
 * the lamp, dark holes/background, <=1px segment-column gaps. Arbitrary fonts
 * and severely underresolved displays are unsupported. A circular object with
 * the same bright colour/shape/dark surroundings as a lamp, or a broken glyph
 * identical to another valid digit, cannot be semantically distinguished by
 * these appearance rules; low/ambiguous evidence is rejected. Multiple plausible lamps (even the same
 * colour) are UNKNOWN; no lane/direction semantics are inferred. Modest
 * illumination/neutral veil tolerance does not restore lost scene detail.
 * Adjacent red/amber fragments may form one warm lamp outline. Mixed hue
 * recovery requires sufficient unclipped colour evidence; red recovery also
 * needs a surrounding rim and rejects clearly-yellow clipped evidence.
 * Individually credible same/different-colour lamps remain ambiguous. */
int traffic_vision_process(traffic_vision_t *state,const uint16_t *frame,
                           unsigned width,unsigned height,traffic_vision_result_t *result);

#endif
