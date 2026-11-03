#include "traffic_service.h"
#include "traffic_vision_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t reg_status,read_addr,control,hud,magic=0x54563035u;
static unsigned arms,processes,resets,data_reads,read_cost_us,slices;
static uint64_t clock_us=100000;
uint64_t traffic_test_now_us(void) { return clock_us; }
uint32_t traffic_test_read(unsigned offset) {
    switch(offset) {
        case 0xa4:return magic;case 0x78:return control;case 0x94:return reg_status;
        case 0x98:return read_addr;
        case 0x9c:++data_reads;clock_us+=read_cost_us;return (read_addr&65535u)|((read_addr^65535u)<<16);
        default: assert(0);return 0;
    }
}
void traffic_test_write(unsigned off,uint32_t v) {
    switch(off) { case 0x90:++arms;reg_status=2;break;
        case 0x98: assert(v<38400);read_addr=v;break;case 0xa0:hud=v;break;
        default:assert(0); }
}
void traffic_vision_init(traffic_vision_t *v) { memset(v,0,sizeof *v); }
void traffic_vision_reset(traffic_vision_t *v) { (void)v;++resets; }
int traffic_vision_process(traffic_vision_t *v,const uint16_t *p,unsigned w,unsigned h,traffic_vision_result_t *out) {
    unsigned i;(void)v; assert(w==320 && h==240);
    for(i=0;i<38400;++i) { assert(p[2*i]==(uint16_t)i);assert(p[2*i+1]==(uint16_t)(i^65535u)); }
    memset(out,0,sizeof *out);out->color=3;out->valid=1;out->confidence=90;
    out->countdown_valid=1;out->countdown=28;++processes;clock_us+=1000;return 0;
}
static void ready(unsigned gen) { reg_status=(gen<<16)|1; }
static void poll(void) {
    unsigned before=data_reads;traffic_service_poll();
    assert(data_reads-before<=1024u);
    if(read_cost_us==10u)assert(data_reads-before<=128u); /* 64-word budget checks */
    ++slices;
}
static void polls(unsigned n) { while(n--)poll(); }
int main(void) {
    traffic_snapshot_t s;traffic_frame_snapshot_t f;unsigned prior;
    traffic_service_init(); traffic_service_get_snapshot(&s);assert(!s.valid && s.countdown==-1);
    poll();assert(arms==1);ready(1);poll();assert(data_reads==1024u);polls(149);
    traffic_service_get_snapshot(&s);assert(processes==1 && s.seq==1 && s.valid && s.color==3 && s.digit_valid && s.countdown==28 && s.proc_ms==1);
    {traffic_frame_snapshot_t a,b;unsigned before_reads=data_reads,before_arms=arms;
     assert(!traffic_service_peek_frame(NULL));
     assert(traffic_service_peek_frame(&a)&&traffic_service_peek_frame(&b));
     assert(a.pixels==b.pixels&&a.sequence==b.sequence&&a.capture_us==b.capture_us);
     assert(data_reads==before_reads&&arms==before_arms); /* peek neither recaptures nor consumes */
    }
    assert(!traffic_service_take_frame(NULL));
    assert(traffic_service_take_frame(&f)&&f.pixels&&f.width==320u&&f.height==240u&&f.source_frame==1u&&f.sequence==1u&&f.capture_us==100000u);
    assert(f.pixels[0]==0u&&f.pixels[1]==65535u&&!traffic_service_take_frame(&f));
    assert((hud&15u)==15u);prior=processes;clock_us+=1600000;
    traffic_service_get_snapshot(&s);assert(!s.valid && s.countdown==-1);traffic_service_poll();assert(!(hud&4u));
    ready(2);polls(12);ready(3);traffic_service_poll();assert(processes==prior);assert(resets>0);
    clock_us+=2100000;traffic_service_poll();ready(4);polls(5);control=0x80;traffic_service_poll();
    assert(processes==prior);traffic_service_get_snapshot(&s);assert(!s.valid && s.countdown==-1);
    control=0;clock_us+=300000;traffic_service_poll();reg_status=4;traffic_service_poll();assert(processes==prior);
    clock_us+=2100000;traffic_service_poll();clock_us+=2100000;traffic_service_poll();assert(processes==prior);
    clock_us+=300000;traffic_service_init();traffic_service_poll();ready(10);
    clock_us+=1400000;polls(150);traffic_service_get_snapshot(&s);
    assert(s.valid && s.age_ms>=1400 && s.age_ms<1500); /* age starts before capture, not at CPU observation */
    traffic_service_suspend();assert(!traffic_service_take_frame(&f));traffic_service_get_snapshot(&s);assert(!s.valid && s.countdown==-1 && !(hud&4u));
    clock_us+=300000;traffic_service_poll();ready(11);clock_us+=1600000;polls(150);
    traffic_service_get_snapshot(&s);assert(!s.valid && s.countdown==-1);
    /* Simulated 10us per MMIO word: yield after 128 words (1.28ms), not
     * after the 1024-word cap. This tests scheduling, not ARM performance. */
    clock_us+=300000;traffic_service_init();read_cost_us=10;poll();ready(30);
    prior=data_reads;poll();assert(data_reads-prior==128u);polls(299);
    traffic_service_get_snapshot(&s);assert(s.valid&&s.age_ms>=384&&s.age_ms<1500);
    prior=processes;clock_us+=300000;poll();ready(30);poll();
    traffic_service_get_snapshot(&s);assert(processes==prior&&!s.valid); /* one gen cannot confirm twice */
    clock_us+=300000;read_cost_us=0;poll();ready(31);polls(38);
    traffic_service_get_snapshot(&s);assert(s.valid&&processes==prior+1u);
    /* Cross the common 32-bit microsecond boundary during a real copy.
     * All service timestamps and elapsed differences must stay 64-bit. */
    clock_us=0xfffff000ull;read_cost_us=10;traffic_service_init();poll();ready(40);polls(300);
    traffic_service_get_snapshot(&s);assert(clock_us>0xffffffffull&&s.valid&&s.age_ms>=384&&s.age_ms<1500);
    /* Frozen-generation counter wraps at 16 bits; only an identical previous
     * generation is a duplicate. FFFF -> 0000 must remain a new observation. */
    read_cost_us=0;traffic_service_init();poll();ready(65535);polls(38);prior=processes;
    clock_us+=300000;poll();ready(0);polls(38);traffic_service_get_snapshot(&s);assert(s.valid&&processes==prior+1u);
    magic=0;traffic_service_init();prior=arms;polls(10);assert(arms==prior);
    traffic_service_get_snapshot(&s);assert(!s.valid && s.countdown==-1 && s.age_ms>1500);
    puts("TRAFFIC_SERVICE_TEST_PASS: frozen generation, 1024-word cap + timed yields, duplicate-gen rejection + 16-bit wrap, 64-bit elapsed crossing, exact pixel order, stale source, JPEG transition, bad/timeout/old hardware");return 0;
}

