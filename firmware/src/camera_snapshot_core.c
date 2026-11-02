#include "camera_snapshot_core.h"
#include "app_features.h"
#define STILL_CONTROL (12u|128u)
#define ARM_BIT (1u<<11)
#define CONFIG_TIMEOUT_US 2000000u
#define CAPTURE_TIMEOUT_US 12000000u
#if APP_ENABLE_PHOTO_SETTLE
#ifndef APP_PHOTO_SETTLE_MIN_US
#define APP_PHOTO_SETTLE_MIN_US 1200000u
#endif
#define SETTLE_TIMEOUT_US 8000000u
#if APP_PHOTO_SETTLE_MIN_US == 0 || APP_PHOTO_SETTLE_MIN_US > SETTLE_TIMEOUT_US
#error "Photo settle minimum must be within the eight-second deadline"
#endif
#define SETTLE_FRAMES 6u

/* Sensor mode changes reset AEC/AWB. Count raw VSYNC falling edges even while
 * the RGB preview bridge is held in reset for JPEG mode. Config completion is
 * a bus/stream-on acknowledgement, not evidence that AEC/AWB has converged. */
static photo_capture_error_t wait_photo_settle(const photo_io_t *io,
                                              photo_capture_result_t *result)
{
    uint64_t start=io->now_us(io->context);
    uint8_t previous=(uint8_t)(io->read(io->context,PHOTO_REG_DIAG3)>>16);
    for(;;) {
        uint32_t d=io->read(io->context,PHOTO_REG_DIAG1);
        uint64_t elapsed=io->now_us(io->context)-start;
        uint8_t current=(uint8_t)(io->read(io->context,PHOTO_REG_DIAG3)>>16);
        /* An eight-bit modular difference also handles 255 -> 0 rollover. */
        result->settle_frames+=(uint8_t)(current-previous);
        result->settle_ms=(uint32_t)(elapsed/1000u);
        previous=current;
        if((d>>16)!=0xca01u || (d&8u) || !(d&16u))
            return PHOTO_CAPTURE_SETTLE_CFG_LOST;
        if(elapsed>SETTLE_TIMEOUT_US) return PHOTO_CAPTURE_SETTLE_TIMEOUT;
        if(result->settle_frames>=SETTLE_FRAMES && elapsed>=APP_PHOTO_SETTLE_MIN_US)
            return PHOTO_CAPTURE_OK;
        if(elapsed>=SETTLE_TIMEOUT_US) return PHOTO_CAPTURE_SETTLE_TIMEOUT;
        io->sleep_us(io->context,1000);
    }
}
#endif

static photo_capture_error_t wait_configuration(const photo_io_t *io,int restoring)
{
    uint64_t start=io->now_us(io->context);
    for(;;) {
        uint32_t d=io->read(io->context,PHOTO_REG_DIAG1);
        if((d>>16)!=0xca01u || (d&8u))
            return restoring ? PHOTO_CAPTURE_RESTORE_ERROR : PHOTO_CAPTURE_CFG_ERROR;
        if(d&16u) return PHOTO_CAPTURE_OK;
        if(io->now_us(io->context)-start>=CONFIG_TIMEOUT_US)
            return restoring ? PHOTO_CAPTURE_RESTORE_TIMEOUT : PHOTO_CAPTURE_CFG_TIMEOUT;
        io->sleep_us(io->context,100);
    }
}

static void configure(const photo_io_t *io,uint32_t control)
{
    io->write(io->context,PHOTO_REG_CONTROL,control|1u);
    io->sleep_us(io->context,10000);
    io->write(io->context,PHOTO_REG_CONTROL,control);
}

photo_capture_error_t photo_capture_acquire(const photo_io_t *io,uint8_t *buffer,
                                          size_t capacity,photo_capture_result_t *result)
{
    photo_capture_error_t error=PHOTO_CAPTURE_OK,restore;
    uint64_t start;
    uint32_t still_control;
    size_t copied=0;
    result->size=0; result->saved_control=0; result->reconfigured=0;
    result->settle_frames=0; result->settle_ms=0;
    if(!buffer || !capacity || (io->read(io->context,PHOTO_REG_DIAG1)>>16)!=0xca01u)
        return PHOTO_CAPTURE_NO_HW;
    result->saved_control=io->read(io->context,PHOTO_REG_CONTROL)&~(1u|128u|ARM_BIT);
    /* HP DDR readiness belongs to the initialized PS, not the sensor mode.
     * Never drop it while switching to JPEG and back to the preview. */
    still_control=STILL_CONTROL|(result->saved_control&PHOTO_CONTROL_DDR_READY);
    result->reconfigured=1;
    configure(io,still_control);
    error=wait_configuration(io,0);
    if(error!=PHOTO_CAPTURE_OK) goto restore_preview;
#if APP_ENABLE_PHOTO_SETTLE
    error=wait_photo_settle(io,result);
    if(error!=PHOTO_CAPTURE_OK) goto restore_preview;
#endif
    io->write(io->context,PHOTO_REG_CONTROL,still_control|ARM_BIT);
    start=io->now_us(io->context);
    for(;;) {
        uint32_t status=io->read(io->context,PHOTO_REG_STATUS);
        uint32_t available=io->read(io->context,PHOTO_REG_WORDS);
        uint32_t total=(status&PHOTO_STATUS_DONE) ? io->read(io->context,PHOTO_REG_BYTES) : 0;
        uint32_t batch=available>256u ? 256u : available;
        unsigned i;
        if(status&PHOTO_STATUS_OVERFLOW) { error=PHOTO_CAPTURE_OVERFLOW; break; }
        if(status&PHOTO_STATUS_TIMEOUT) { error=PHOTO_CAPTURE_PL_TIMEOUT; break; }
        if(io->now_us(io->context)-start>=CAPTURE_TIMEOUT_US) { error=PHOTO_CAPTURE_DEADLINE; break; }
        if((status&PHOTO_STATUS_DONE) && total>capacity) { error=PHOTO_CAPTURE_TOO_LARGE; break; }
        if((status&PHOTO_STATUS_DONE) && (size_t)((total+3u)&~3u)<copied) {
            error=PHOTO_CAPTURE_COUNT; break;
        }
        for(i=0;i<batch;++i) {
            unsigned j;
            uint32_t word;
            if(status&PHOTO_STATUS_DONE) {
                if(copied>=(size_t)((total+3u)&~3u)) { error=PHOTO_CAPTURE_COUNT; break; }
            }
            if(4u>capacity-copied) { error=PHOTO_CAPTURE_TOO_LARGE; break; }
            word=io->read(io->context,PHOTO_REG_DATA);
            for(j=0;j<4;++j) buffer[copied++]=(uint8_t)(word>>(j*8));
        }
        if(error!=PHOTO_CAPTURE_OK) break;
        if((status&PHOTO_STATUS_DONE) && io->read(io->context,PHOTO_REG_WORDS)==0) {
            if(copied!=(size_t)((total+3u)&~3u) || !total) error=PHOTO_CAPTURE_COUNT;
            else if(!(status&PHOTO_STATUS_SOI)) error=PHOTO_CAPTURE_NO_SOI;
            else copied=total;
            break;
        }
    }
restore_preview:
    configure(io,result->saved_control);
    restore=wait_configuration(io,1);
    if(restore!=PHOTO_CAPTURE_OK) error=restore;
    if(error==PHOTO_CAPTURE_OK) result->size=copied;
    return error;
}

const char *photo_capture_error_name(photo_capture_error_t error)
{
    static const char *const names[]={"OK","NO_CAMERA_HW","JPEG_CONFIG","JPEG_CONFIG_TIMEOUT",
        "FIFO_OVERFLOW","PL_TIMEOUT","CAPTURE_DEADLINE","PHOTO_TOO_LARGE","BYTE_COUNT",
        "NO_SOI","PREVIEW_RESTORE","PREVIEW_RESTORE_TIMEOUT",
        "JPEG_SETTLE_TIMEOUT","JPEG_SETTLE_CFG_LOST"};
    return (unsigned)error<sizeof names/sizeof names[0] ? names[error] : "UNKNOWN";
}
