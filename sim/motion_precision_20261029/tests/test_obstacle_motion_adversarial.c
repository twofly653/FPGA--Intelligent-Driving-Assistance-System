#include "obstacle_motion_core.h"

#include <stdio.h>
#include <string.h>

/* Pure host regression: synthetic RGB565 frames and injected association state.
 * No board, camera, serial, JTAG, hardware server, or firmware download access. */
static obstacle_motion_t state;
static obstacle_motion_result_t result;
static uint16_t frame[OBSTACLE_MOTION_INPUT_WIDTH * OBSTACLE_MOTION_INPUT_HEIGHT];
static uint32_t sequence, milliseconds;
static unsigned assertions, failures;

#define EXPECT(c) do { \
    ++assertions; \
    if (!(c)) { \
        ++failures; \
        fprintf(stderr, "FAIL %s:%d: %s (status=%u count=%u changed=%u fg=%u)\n", \
                __FILE__, __LINE__, #c, result.status, result.count, \
                result.changed_pixels, result.foreground_pixels); \
    } \
} while (0)

static uint16_t gray(unsigned level)
{
    return (uint16_t)(((level >> 3) << 11) | ((level >> 2) << 5) | (level >> 3));
}

static void blank(void)
{
    memset(frame, 0, sizeof(frame));
}

static void rect(unsigned x, unsigned y, unsigned w, unsigned h, unsigned level)
{
    unsigned xx, yy;
    for (yy = y; yy < y + h; ++yy)
        for (xx = x; xx < x + w; ++xx)
            frame[yy * OBSTACLE_MOTION_INPUT_WIDTH + xx] = gray(level);
}

static void run(void)
{
    ++sequence;
    milliseconds += 100u;
    EXPECT(obstacle_motion_process(&state, frame,
        OBSTACLE_MOTION_INPUT_WIDTH, OBSTACLE_MOTION_INPUT_HEIGHT,
        sequence, milliseconds, milliseconds, &result) == 0);
    EXPECT(result.count <= OBSTACLE_MOTION_MAX_OBJECTS);
}

static void start(void)
{
    unsigned i;
    obstacle_motion_init(&state);
    sequence = 0;
    milliseconds = 1000u;
    for (i = 0; i < 4u; ++i) {
        blank();
        run();
        EXPECT(result.count == 0u);
        EXPECT(result.status == (i < 3u ? OBSTACLE_MOTION_WARMUP : OBSTACLE_MOTION_READY));
    }
}

static const obstacle_motion_object_t *near(unsigned cx, unsigned cy)
{
    unsigned i;
    for (i = 0; i < result.count; ++i) {
        const obstacle_motion_bbox_t *b = &result.objects[i].bbox;
        unsigned bx = b->x + b->w / 2u, by = b->y + b->h / 2u;
        if (bx + 3u >= cx && bx <= cx + 3u && by + 3u >= cy && by <= cy + 3u)
            return &result.objects[i];
    }
    return NULL;
}

static void track(unsigned slot, uint16_t id, unsigned x, unsigned y,
                  unsigned w, unsigned h)
{
    obstacle_motion_track_t *t = &state.tracks[slot];
    memset(t, 0, sizeof(*t));
    t->used = 1u; t->confirmed=1u;t->hits=3u;
    t->chroma_mean[0]=128u;t->chroma_mean[1]=128u;
    t->id = id;
    t->bbox.x = (uint16_t)x; t->bbox.y = (uint16_t)y;
    t->bbox.w = (uint16_t)w; t->bbox.h = (uint16_t)h;
    t->last_seen_ms = milliseconds;
}

static void checkerboard(unsigned left, unsigned dark)
{
    unsigned x, y;
    blank();
    /* 128 x 120 input pixels: 20% of the frame, straddling both centre lines.
     * The camera and its background remain completely stationary. */
    for (y = 60u; y < 180u; ++y)
        for (x = left; x < left + 128u; ++x)
            frame[y * OBSTACLE_MOTION_INPUT_WIDTH + x] =
                gray((((x - left) / 8u + (y - 60u) / 8u) & 1u) ? 255u : dark);
}

static void test_local_texture_is_not_camera_shift(void)
{
    unsigned before = failures;
    start();
    checkerboard(92u,64u);run();EXPECT(result.count==0u);
    checkerboard(96u,64u);run();EXPECT(result.count==0u);
    checkerboard(100u,64u);run();
    EXPECT(result.changed_pixels * 100u < OBSTACLE_MOTION_PIXELS * 35u);
    EXPECT(result.status == OBSTACLE_MOTION_READY && result.count == 1u);
    EXPECT(result.count == 1u && result.objects[0].motion_pixels >= 4u);
    printf("CASE local-texture-camera-shift: %s\n", failures == before ? "PASS" : "FAIL");
}

static void test_texture_matching_background_tone(void)
{
    unsigned i, before = failures;
    start();
    /* Half of the object's texture equals the black background. The detector
     * may legitimately split its visible contrast into several observations;
     * it must still retain current observations instead of calling it a camera
     * shift and resetting its history. */
    checkerboard(92u,0u);run();EXPECT(result.count==0u);
    checkerboard(96u,0u);run();EXPECT(result.count==0u);
    checkerboard(100u,0u);run();
    EXPECT(result.changed_pixels * 100u < OBSTACLE_MOTION_PIXELS * 35u);
    EXPECT(result.status == OBSTACLE_MOTION_READY && result.count > 0u);
    for (i = 0; i < result.count; ++i) EXPECT(result.objects[i].motion_pixels >= 4u);
    printf("CASE texture-matching-background-tone: %s\n", failures == before ? "PASS" : "FAIL");
}

static void test_maximum_feasible_association(void)
{
    unsigned before = failures;
    const obstacle_motion_object_t *left, *right;
    start();
    /* Association-only setup. Both previous tracks are live and stationary.
     * Their analysis-space centres are (30,50), (60,50), and the next measured
     * components are (15,50), (40,50). Width 12 gives a 36-pixel centre gate:
     * 30->15 and 60->40 are both feasible; 60->15 is not. A greedy choice of
     * 30->40 needlessly loses the otherwise feasible second association. */
    track(0u, 101u, 24u, 44u, 12u, 12u);
    track(1u, 202u, 54u, 44u, 12u, 12u);
    state.next_id = 303u;
    blank();
    rect(20u, 90u, 20u, 20u, 224u);
    rect(70u, 90u, 20u, 20u, 224u);
    run();
    EXPECT(result.status == OBSTACLE_MOTION_READY && result.count == 2u);
    left = near(30u, 100u); right = near(80u, 100u);
    EXPECT(left != NULL && right != NULL);
    EXPECT(left != NULL && left->id == 101u);
    EXPECT(right != NULL && right->id == 202u);
    printf("CASE maximum-feasible-association: %s", failures == before ? "PASS" : "FAIL");
    if (left && right) printf(" (left_id=%u right_id=%u)", left->id, right->id);
    putchar('\n');
}

static void test_wrapped_id_skips_live_track(void)
{
    unsigned before = failures;
    const obstacle_motion_object_t *existing, *new_object;
    start();
    track(0u, 1u, 4u, 4u, 12u, 12u);
    /* Represents the uint16_t sequence just having wrapped while ID 1 is
     * still live. Setting it directly avoids tens of thousands of frames. */
    state.next_id = 1u;
    blank();
    rect(10u, 10u, 20u, 20u, 224u);
    rect(240u, 150u, 20u, 20u, 224u);
    run();
    EXPECT(result.status == OBSTACLE_MOTION_READY && result.count == 1u);
    EXPECT(state.tracks[1].used && state.tracks[1].id!=1u && state.tracks[1].id!=0u);
    blank();rect(12u,10u,20u,20u,224u);rect(242u,150u,20u,20u,224u);run();
    blank();rect(14u,10u,20u,20u,224u);rect(246u,150u,20u,20u,224u);run();
    existing = near(24u, 20u); new_object = near(256u, 160u);
    EXPECT(existing != NULL && new_object != NULL);
    EXPECT(existing != NULL && existing->id == 1u);
    EXPECT(new_object != NULL && new_object->id != 0u && new_object->id != 1u);
    EXPECT(result.count == 2u && result.objects[0].id != result.objects[1].id);
    printf("CASE wrapped-id-skips-live-track: %s", failures == before ? "PASS" : "FAIL");
    if (existing && new_object) printf(" (existing_id=%u new_id=%u)", existing->id, new_object->id);
    putchar('\n');
}

int main(int argc, char **argv)
{
    int selected = 0;
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "camera-shift") == 0)) {
        test_local_texture_is_not_camera_shift(); selected = 1;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "camera-shift-matched-tone") == 0)) {
        test_texture_matching_background_tone(); selected = 1;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "matching") == 0)) {
        test_maximum_feasible_association(); selected = 1;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "id-wrap") == 0)) {
        test_wrapped_id_skips_live_track(); selected = 1;
    }
    if (!selected) {
        fprintf(stderr, "usage: %s [camera-shift|camera-shift-matched-tone|matching|id-wrap]\n", argv[0]);
        return 2;
    }
    printf("OBSTACLE_MOTION_ADVERSARIAL_%s: %u assertions, %u failures\n",
           failures ? "FAIL" : "PASS", assertions, failures);
    return failures ? 1 : 0;
}
