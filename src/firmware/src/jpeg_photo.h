#ifndef JPEG_PHOTO_H
#define JPEG_PHOTO_H
#include <stddef.h>
#include <stdint.h>
typedef enum {
    JPEG_PHOTO_OK=0, JPEG_PHOTO_TRUNCATED, JPEG_PHOTO_MARKER,
    JPEG_PHOTO_UNSUPPORTED, JPEG_PHOTO_DIMENSIONS
} jpeg_photo_error_t;
/* Structural marker validation, not an entropy decoder. No rescaling occurs. */
jpeg_photo_error_t jpeg_photo_validate(const uint8_t *bytes,size_t size,
                                     unsigned *width,unsigned *height);
uint32_t jpeg_photo_crc32(const uint8_t *bytes,size_t size);
#endif
