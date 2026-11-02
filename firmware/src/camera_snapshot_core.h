#ifndef CAMERA_SNAPSHOT_CORE_H
#define CAMERA_SNAPSHOT_CORE_H
#include <stddef.h>
#include <stdint.h>
#define PHOTO_REG_DIAG1 0x5cu
#define PHOTO_REG_DIAG3 0x64u
#define PHOTO_REG_CONTROL 0x78u
#define PHOTO_REG_STATUS 0x7cu
#define PHOTO_REG_BYTES 0x80u
#define PHOTO_REG_DATA 0x84u
#define PHOTO_REG_WORDS 0x88u
#define PHOTO_STATUS_BUSY 1u
#define PHOTO_STATUS_DONE 2u
#define PHOTO_STATUS_OVERFLOW 4u
#define PHOTO_STATUS_TIMEOUT 8u
#define PHOTO_STATUS_SOI 16u
#define PHOTO_CONTROL_DDR_READY (1u<<15)
typedef struct {
    void *context;
    uint32_t (*read)(void *context,unsigned offset);
    void (*write)(void *context,unsigned offset,uint32_t value);
    uint64_t (*now_us)(void *context);
    void (*sleep_us)(void *context,unsigned duration);
} photo_io_t;
typedef enum {
    PHOTO_CAPTURE_OK=0, PHOTO_CAPTURE_NO_HW, PHOTO_CAPTURE_CFG_ERROR,
    PHOTO_CAPTURE_CFG_TIMEOUT, PHOTO_CAPTURE_OVERFLOW, PHOTO_CAPTURE_PL_TIMEOUT,
    PHOTO_CAPTURE_DEADLINE, PHOTO_CAPTURE_TOO_LARGE, PHOTO_CAPTURE_COUNT,
    PHOTO_CAPTURE_NO_SOI, PHOTO_CAPTURE_RESTORE_ERROR, PHOTO_CAPTURE_RESTORE_TIMEOUT,
    PHOTO_CAPTURE_SETTLE_TIMEOUT, PHOTO_CAPTURE_SETTLE_CFG_LOST
} photo_capture_error_t;
typedef struct {
    size_t size;
    uint32_t saved_control;
    int reconfigured;
    uint32_t settle_frames,settle_ms;
} photo_capture_result_t;
/* Blocking and bounded. Only the caller's buffer is written; it is not DMA.
 * FIFO words are FWFT, little endian; final padding is excluded using BYTES. */
photo_capture_error_t photo_capture_acquire(const photo_io_t *io,uint8_t *buffer,
                                          size_t capacity,photo_capture_result_t *result);
const char *photo_capture_error_name(photo_capture_error_t error);
#endif
