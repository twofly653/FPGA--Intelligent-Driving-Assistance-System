#include "camera_snapshot.h"
#include "app_features.h"
#include "camera_snapshot_core.h"
#include "jpeg_photo.h"
#include "pl_hub.h"
#include "attitude_est.h"
#include "telemetry.h"
#include "traffic_service.h"
#include "obstacle_service.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xtime_l.h"
#include "sleep.h"
#include <string.h>

#define PHOTO_CAPACITY (2u*1024u*1024u)
static uint8_t g_photo[PHOTO_CAPACITY] __attribute__((aligned(32)));
static unsigned g_id,g_valid,g_requested,g_status_requested,g_busy,g_wait_clear;
static size_t g_size;
static uint32_t g_crc;
static uint64_t g_clear_started;
static const char *g_error;

static uint32_t photo_read(void *context,unsigned offset)
{ (void)context; return pl_rd(offset); }
static void photo_write(void *context,unsigned offset,uint32_t value)
{ (void)context; pl_wr(offset,value); }
static uint64_t photo_now_us(void *context)
{
    XTime now;
    (void)context;
    XTime_GetTime(&now);
    return (uint64_t)(now/COUNTS_PER_SECOND)*1000000u+
           (uint64_t)((now%COUNTS_PER_SECOND)*1000000u/COUNTS_PER_SECOND);
}
static void photo_sleep_us(void *context,unsigned duration)
{ (void)context; usleep(duration); }
static const photo_io_t g_io={0,photo_read,photo_write,photo_now_us,photo_sleep_us};

void camera_snapshot_init(void)
{
    g_id=g_valid=g_requested=g_status_requested=g_busy=g_wait_clear=0;
    g_size=0; g_crc=0; g_error=0; g_clear_started=0;
}

int camera_snapshot_command(const char *line)
{
    if(!strcmp(line,"PHOTO SNAP")) {
        if(g_busy || g_requested) g_status_requested=1;
        else g_requested=1;
        return 1;
    }
    if(!strcmp(line,"PHOTO STATUS")) { g_status_requested=1; return 1; }
    return 0;
}

static void report_status(void)
{
    telemetry_flush_pending();
    if(g_busy || g_requested) xil_printf("PHOTO BUSY %u\r\n",g_id);
    else if(g_valid) xil_printf("PHOTO READY %u %08x %u 2592 1944 %08x\r\n",
        g_id,(unsigned)(UINTPTR)g_photo,(unsigned)g_size,(unsigned)g_crc);
    else if(g_error) xil_printf("PHOTO ERROR %s\r\n",g_error);
    else xil_printf("PHOTO IDLE\r\n");
}

void camera_snapshot_poll(void)
{
    photo_capture_result_t result;
    photo_capture_error_t capture;
    jpeg_photo_error_t jpeg;
    unsigned width,height;
    if(g_status_requested) { g_status_requested=0; report_status(); }
    if(g_requested && !g_busy) {
        g_requested=0; g_busy=1; g_valid=0; g_error=0; g_size=0;
        ++g_id;
        traffic_service_suspend();
        obstacle_service_suspend();
        g_wait_clear=1;g_clear_started=photo_now_us(0);
        report_status();
    }
    if(!g_busy || !g_wait_clear) return;
    /* Keep the normal main loop alive while a prior box mailbox drains.
     * Analysis remains paused so no new box can race the empty commit. */
    if(!obstacle_service_suspend_ready()) {
        if(photo_now_us(0)-g_clear_started>1000000u) {
            g_wait_clear=0;g_busy=0;g_error="OBSTACLE_CLEAR_TIMEOUT";
            traffic_service_suspend();obstacle_service_resume();report_status();
        }
        return;
    }
    g_wait_clear=0;
    traffic_service_suspend();
    /* Capture owns the CPU temporarily: no LCD drawing, IMU polling, telemetry
     * generation or second UART reader is called from the FIFO drain loop. */
    capture=photo_capture_acquire(&g_io,g_photo,sizeof g_photo,&result);
#if APP_ENABLE_PHOTO_SETTLE
    xil_printf("[PHOTO] settle frames=%u ms=%u result=%s\r\n",
        (unsigned)result.settle_frames,(unsigned)result.settle_ms,
        photo_capture_error_name(capture));
#endif
    if(result.reconfigured) attitude_est_request_realign();
    if(capture!=PHOTO_CAPTURE_OK) g_error=photo_capture_error_name(capture);
    else {
        jpeg=jpeg_photo_validate(g_photo,result.size,&width,&height);
        if(jpeg!=JPEG_PHOTO_OK) g_error=jpeg==JPEG_PHOTO_DIMENSIONS ? "NOT_5MP" : "INVALID_JPEG";
        else {
            g_size=result.size;
            g_crc=jpeg_photo_crc32(g_photo,g_size);
            Xil_DCacheFlushRange((INTPTR)g_photo,(u32)g_size);
            g_valid=1;
        }
    }
    traffic_service_suspend();
    obstacle_service_resume();
    g_busy=0;
    report_status();
}
