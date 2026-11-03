#include "traffic_service.h"
#include "traffic_vision_core.h"
#include <string.h>
#ifdef TRAFFIC_SERVICE_HOST_TEST
extern uint32_t traffic_test_read(unsigned offset);
extern void traffic_test_write(unsigned offset,uint32_t value);
extern uint64_t traffic_test_now_us(void);
#define read_reg traffic_test_read
#define write_reg traffic_test_write
#define now_us traffic_test_now_us
#else
#include "pl_hub.h"
#include "xtime_l.h"
static uint32_t read_reg(unsigned offset) { return pl_rd(offset); }
static void write_reg(unsigned offset,uint32_t value) { pl_wr(offset,value); }
static uint64_t now_us(void) {
    XTime t; XTime_GetTime(&t);
    return (uint64_t)(t/COUNTS_PER_SECOND)*1000000u+
           (uint64_t)((t%COUNTS_PER_SECOND)*1000000u/COUNTS_PER_SECOND);
}
#endif
#define ARM_REG 0x90u
#define STATUS_REG 0x94u
#define ADDR_REG 0x98u
#define DATA_REG 0x9cu
#define HUD_REG 0xa0u
#define MAGIC_REG 0xa4u
#define FRAME_WORDS (320u*240u/2u)
#define STALE_US 1500000u
#define CAPTURE_MIN_INTERVAL_US 100000u
#define COPY_MAX_WORDS 1024u
#define COPY_SLICE_US 1000u
static traffic_vision_t vision;
static uint16_t pixels[320u*240u];
static traffic_snapshot_t result;
static unsigned available,state,copy_index,generation,heartbeat,have_result;
static unsigned previous_generation,have_generation,frame_pending;
static uint64_t capture_started,ready_time,result_time,last_attempt;

#if defined(TRAFFIC_CAPTURE_DEBUG) && !defined(TRAFFIC_SERVICE_HOST_TEST)
#include "xil_cache.h"
/* Debug-only, never part of the normal ELF. Retain exact failed, immutable
 * analysis frames; flush DDR explicitly for non-coherent JTAG reads. No CPU
 * halt or live-buffer tearing is needed to inspect these optical samples. */
uint16_t traffic_failed_frames[4][320u*240u] __attribute__((aligned(32)));
volatile uint32_t traffic_failed_meta[4][16] __attribute__((aligned(32)));
volatile uint32_t traffic_failed_count[8] __attribute__((aligned(32)));
static void retain_failed_frame(const traffic_vision_result_t *v) {
    unsigned slot=traffic_failed_count[0];
    if(slot>=4u||!v->valid||vision.candidate_digit>=0)return;
    memcpy(traffic_failed_frames[slot],pixels,sizeof pixels);
    traffic_failed_meta[slot][0]=generation;
    traffic_failed_meta[slot][1]=result.seq;
    traffic_failed_meta[slot][2]=v->color;
    traffic_failed_meta[slot][3]=v->confidence;
    traffic_failed_meta[slot][4]=v->diagnostics.digit_candidates;
    traffic_failed_meta[slot][5]=v->diagnostics.digit_rejected;
    traffic_failed_meta[slot][6]=v->diagnostics.conflicts;
    Xil_DCacheFlushRange((INTPTR)traffic_failed_frames[slot],sizeof pixels);
    Xil_DCacheFlushRange((INTPTR)traffic_failed_meta[slot],sizeof traffic_failed_meta[slot]);
    traffic_failed_count[0]=slot+1u;
    Xil_DCacheFlushRange((INTPTR)traffic_failed_count,sizeof traffic_failed_count);
}
#endif

static void write_hud(void) {
    uint32_t w=(result.color&3u)|(result.valid?4u:0u)|(result.digit_valid?8u:0u)|
        ((uint32_t)(result.digit_valid?result.countdown:255)<<8)|((result.confidence&255u)<<16);
    heartbeat^=1u; write_reg(HUD_REG,w|((uint32_t)heartbeat<<31));
}
static void invalidate(void) {
    frame_pending=0;
    traffic_vision_reset(&vision);
    result.color=result.valid=result.confidence=result.digit_valid=0;
    result.countdown=-1;
    if(have_result) { ++result.seq; have_result=0; write_hud(); }
}
void traffic_service_init(void) {
    traffic_vision_init(&vision);
    memset(&result,0,sizeof result); result.countdown=-1; result.age_ms=1501;
    state=copy_index=generation=heartbeat=have_result=0;
    capture_started=ready_time=result_time=last_attempt=0;
    previous_generation=have_generation=frame_pending=0;
#if defined(TRAFFIC_CAPTURE_DEBUG) && !defined(TRAFFIC_SERVICE_HOST_TEST)
    memset((void *)traffic_failed_count,0,sizeof traffic_failed_count);
    Xil_DCacheFlushRange((INTPTR)traffic_failed_count,sizeof traffic_failed_count);
#endif
    available=read_reg(MAGIC_REG)==0x54563035u;
    if(available) write_hud();
}
void traffic_service_suspend(void) {
    state=0; copy_index=0; last_attempt=0;
    if(available) {
        unsigned was=have_result;
        invalidate();
        if(!was) write_hud();
    }
}
void traffic_service_get_snapshot(traffic_snapshot_t *out) {
    uint64_t t=now_us(),age=have_result && t>=result_time?(t-result_time)/1000u:1501u;
    if(!out) return;
    *out=result; out->age_ms=age>0x7fffffffu?0x7fffffffu:(uint32_t)age;
    if(!available || age>STALE_US/1000u) {
        out->color=out->valid=out->confidence=out->digit_valid=0; out->countdown=-1;
    }
}
int traffic_service_peek_frame(traffic_frame_snapshot_t *out) {
    uint64_t t=now_us();
    if(!out)return 0;
    memset(out,0,sizeof *out);
    if(!available||!frame_pending||state!=0u||t<capture_started||
       t-capture_started>STALE_US)return 0;
    out->pixels=pixels;out->width=320u;out->height=240u;
    out->source_frame=generation;out->sequence=result.seq;
    out->capture_us=capture_started;
    return 1;
}
int traffic_service_take_frame(traffic_frame_snapshot_t *out) {
    if(!traffic_service_peek_frame(out))return 0;
    frame_pending=0;return 1;
}
void traffic_service_poll(void) {
    unsigned st,n; uint64_t t=now_us();
    if(!available) return;
    if(have_result && t-result_time>STALE_US) invalidate();
    /* Still capture resets only this analysis producer. Discard a partial CPU
     * copy and history instead of ever mixing RGB with JPEG/recovery pixels. */
    if(read_reg(0x78u)&0x81u) { state=0; invalidate(); return; }
    st=read_reg(STATUS_REG);
    if(state==0) {
        if(last_attempt && t-last_attempt<CAPTURE_MIN_INTERVAL_US) return;
        frame_pending=0; write_reg(ARM_REG,1); capture_started=last_attempt=t; state=1; return;
    }
    if(state==1) {
        if(st&4u || t-capture_started>2000000u) { state=0; invalidate(); return; }
        if(!(st&1u)) return;
        generation=st>>16;
        if(have_generation && generation==previous_generation) {state=0;invalidate();return;}
        ready_time=t; copy_index=0; state=2;
    }
    if(state==2) {
        if(!(st&1u) || st>>16!=generation || t-ready_time>STALE_US) {
            state=0; invalidate(); return;
        }
        uint64_t slice_started=now_us();
        /* Up to 1024 words per call, yielding after the first 64-word block
         * that reaches 1ms. Avoid 150 LCD-interleaved calls per frame while
         * retaining a measured time budget for input/100Hz IMU. MMIO timing
         * and total frame cadence must still be measured on the target. */
        for(n=0;n<COPY_MAX_WORDS && copy_index<FRAME_WORDS;++n) {
            uint32_t w;
            write_reg(ADDR_REG,copy_index);
            (void)read_reg(ADDR_REG); /* drain posted address write + BRAM latency */
            w=read_reg(DATA_REG);
            pixels[copy_index*2u]=(uint16_t)w; pixels[copy_index*2u+1u]=(uint16_t)(w>>16);
            ++copy_index;
            if((n&63u)==63u && now_us()-slice_started>=COPY_SLICE_US) break;
        }
        if(copy_index==FRAME_WORDS) {
            traffic_vision_result_t v; uint64_t begin=now_us(),end;
            st=read_reg(STATUS_REG);
            if(!(st&1u) || st>>16!=generation) { state=0; invalidate(); return; }
            previous_generation=generation;have_generation=1;
            (void)traffic_vision_process(&vision,pixels,320,240,&v);
            end=now_us();
#if defined(TRAFFIC_CAPTURE_DEBUG) && !defined(TRAFFIC_SERVICE_HOST_TEST)
            retain_failed_frame(&v);
#endif
            ++result.seq; result.capture_ms=(uint32_t)((ready_time-capture_started+999u)/1000u);
            result.copy_ms=(uint32_t)((begin-ready_time+999u)/1000u); result.proc_ms=(uint32_t)((end-begin+999u)/1000u);
            result.diagnostic_candidates=v.diagnostics.digit_candidates;
            result.diagnostic_rejected=v.diagnostics.digit_rejected;
            result.diagnostic_conflicts=v.diagnostics.conflicts;
            result.diagnostic_digit=vision.candidate_digit;
            result.diagnostic_streak=vision.digit_streak;
            result.color=v.valid?v.color:0; result.valid=v.valid;
            result.confidence=v.valid?v.confidence:0;
            result.digit_valid=v.valid && v.countdown_valid;
            result.countdown=result.digit_valid?v.countdown:-1;
            result_time=capture_started; have_result=1; frame_pending=1; state=0;
            if(end-capture_started>STALE_US) invalidate(); else write_hud();
        }
    }
}
