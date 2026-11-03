#ifndef OBSTACLE_MOTION_CORE_H
#define OBSTACLE_MOTION_CORE_H

#include <stdint.h>

#define OBSTACLE_MOTION_INPUT_WIDTH 320u
#define OBSTACLE_MOTION_INPUT_HEIGHT 240u
#define OBSTACLE_MOTION_WIDTH 160u
#define OBSTACLE_MOTION_HEIGHT 120u
#define OBSTACLE_MOTION_PIXELS (OBSTACLE_MOTION_WIDTH * OBSTACLE_MOTION_HEIGHT)
#define OBSTACLE_MOTION_MAX_OBJECTS 8u
#define OBSTACLE_MOTION_MAX_CANDIDATES 24u
#define OBSTACLE_TRACKING_V2 1
#define OBSTACLE_TRACKING_V3 1
#define OBSTACLE_APPEARANCE_SIDE 6u
#define OBSTACLE_APPEARANCE_SAMPLES 36u

enum {
    OBSTACLE_MOTION_READY = 0,
    OBSTACLE_MOTION_WARMUP = 1,
    OBSTACLE_MOTION_GLOBAL_CHANGE = 2,
    OBSTACLE_MOTION_STALE = 3,
    OBSTACLE_MOTION_DUPLICATE = 4,
    OBSTACLE_MOTION_INVALID = 5
};

typedef struct { uint16_t x, y, w, h; } obstacle_motion_bbox_t;
typedef struct {
    obstacle_motion_bbox_t bbox;  /* 320 x 240 input coordinates, wholly in frame. */
    uint16_t id;                  /* Association ID only, not semantic identity. */
    uint16_t area;                /* Current foreground area in 160 x 120 pixels. */
    uint16_t motion_pixels;       /* Current frame-difference support. */
    uint8_t confidence;           /* Rule support 0..100, NOT measured accuracy. */
} obstacle_motion_object_t;

typedef struct {
    uint32_t frame_id, capture_ms;
    uint16_t changed_pixels, foreground_pixels, components;
    int16_t illumination_delta;
    uint8_t status, count, threshold, background_threshold; /* Last two: luma thresholds. */
    obstacle_motion_object_t objects[OBSTACLE_MOTION_MAX_OBJECTS];
} obstacle_motion_result_t;

typedef struct {
    obstacle_motion_bbox_t bbox;  /* Internal 160 x 120 coordinates. */
    uint32_t last_seen_ms, first_seen_ms;
    obstacle_motion_bbox_t anchor;
    int16_t vx, vy;
    uint16_t id;
    uint8_t used, hits, confirmed, chroma_mean[2];
} obstacle_motion_track_t;

typedef struct {
    obstacle_motion_bbox_t bbox;
    uint16_t area, motion_pixels;
    uint8_t assigned, chroma_mean[2];
} obstacle_motion_component_t;

/* Place the state in static DDR. No malloc, board access, floating point or
 * large automatic arrays are used. Fixed camera / moving-region detector:
 * Gray plus two opponent-colour channels detect near-equal-luma coloured
 * regions. It does not infer people, vehicle class, range, or stationary obstacles. */
typedef struct {
    uint8_t gray[OBSTACLE_MOTION_PIXELS];
    uint8_t smooth[OBSTACLE_MOTION_PIXELS];
    uint8_t previous[OBSTACLE_MOTION_PIXELS];
    uint8_t previous_foreground[OBSTACLE_MOTION_PIXELS];
    uint8_t chroma[2][OBSTACLE_MOTION_PIXELS];
    uint8_t previous_chroma[2][OBSTACLE_MOTION_PIXELS];
    uint16_t background_chroma_q4[2][OBSTACLE_MOTION_PIXELS];
    uint16_t background_q4[OBSTACLE_MOTION_PIXELS];
    uint8_t mask[OBSTACLE_MOTION_PIXELS];
    uint8_t foreground[OBSTACLE_MOTION_PIXELS];
    uint8_t scratch[OBSTACLE_MOTION_PIXELS];
    uint8_t noise_q2[3][OBSTACLE_MOTION_PIXELS]; /* Per-pixel absolute temporal deviation. */
    uint16_t queue[OBSTACLE_MOTION_PIXELS];
    uint16_t histogram[511];
    uint32_t assignment_cost[256], assignment_next_cost[256];
    uint16_t assignment_path[OBSTACLE_MOTION_MAX_OBJECTS][256];
    obstacle_motion_track_t tracks[OBSTACLE_MOTION_MAX_OBJECTS];
    obstacle_motion_component_t candidates[OBSTACLE_MOTION_MAX_CANDIDATES];
    uint32_t previous_frame_id, previous_capture_ms;
    uint16_t next_id;
    uint8_t have_previous, warmup_left, candidate_count;
    /* Image-supported continuation, separate from tentative motion admission. */
    uint8_t stable[OBSTACLE_MOTION_PIXELS];
    uint8_t appearance[OBSTACLE_MOTION_MAX_OBJECTS][3][OBSTACLE_APPEARANCE_SAMPLES];
    uint8_t appearance_valid[OBSTACLE_MOTION_MAX_OBJECTS];
    /* Appended only: preserve historical R11/R12 retained-state prefixes. */
    uint32_t tracking_v3_cookie;
    int16_t velocity_pps[OBSTACLE_MOTION_MAX_OBJECTS][2]; /* Internal pixels/sec. */
    uint8_t velocity_valid[OBSTACLE_MOTION_MAX_OBJECTS];
} obstacle_motion_t;

void obstacle_motion_init(obstacle_motion_t *state);
void obstacle_motion_reset(obstacle_motion_t *state);
/* Immutable original-colour row-major RGB565, exactly 320 x 240.
 * capture_ms and now_ms use the same wrapping millisecond clock.
 * Age > 500 ms, non-increasing capture time, missing input or a source gap
 * > 750 ms invalidates history. First three fresh frames learn the background.
 * A duplicate/out-of-order frame returns DUPLICATE/count=0 without advancing
 * evidence, time, IDs or history; caller must NOT refresh its display heartbeat.
 * STALE/INVALID and source loss must clear the hardware overlay immediately.
 * New tracks need three consecutive observations and measured displacement/size
 * change before display. Single-frame changes are never displayed.
 * New detections require CURRENT motion support. Confirmed tracks may continue
 * through stationary frames or missing components only when current pixels
 * support their foreground and appearance. Prediction alone never draws boxes.
 * Returns 0 for status-bearing results, -1 for invalid arguments/input. */
int obstacle_motion_process(obstacle_motion_t *state, const uint16_t *rgb565,
                            unsigned width, unsigned height, uint32_t frame_id,
                            uint32_t capture_ms, uint32_t now_ms,
                            obstacle_motion_result_t *result);

#endif



