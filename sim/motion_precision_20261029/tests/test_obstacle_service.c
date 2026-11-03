/* Adapter contract tests: real service, mocked detector and register transport.
 * Detector accuracy is independently tested in test_obstacle_motion_core.c. */
#include "obstacle_service.h"
#include "obstacle_motion_core.h"
#include "traffic_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t regs[64],magic=0x4f423031u,busy;
static uint64_t clock_us=1000000;
static unsigned writes,commits,processes,resets,frame_ready,commit_busy;
static uint16_t frame_pixels[320u*240u];
static traffic_frame_snapshot_t frame;
static obstacle_motion_result_t injected;
uint32_t obstacle_test_read(unsigned o){if(o==0xf4)return magic;if(o==0xf0)return busy;assert(0);return 0;}
void obstacle_test_write(unsigned o,uint32_t v){assert(!busy&&o>=0xac&&o<=0xfc&&!(o&3));regs[o/4]=v;++writes;if(o==0xac){++commits;if(commit_busy)busy=1;}}
uint64_t obstacle_test_now_us(void){return clock_us;}
int traffic_service_take_frame(traffic_frame_snapshot_t *f){if(!frame_ready)return 0;*f=frame;frame_ready=0;return 1;}
void obstacle_motion_init(obstacle_motion_t *s){memset(s,0,sizeof *s);}
void obstacle_motion_reset(obstacle_motion_t *s){(void)s;++resets;}
int obstacle_motion_process(obstacle_motion_t *s,const uint16_t *p,unsigned w,unsigned h,uint32_t id,uint32_t cap,uint32_t now,obstacle_motion_result_t *r){
 (void)s;assert(p==frame_pixels&&w==320&&h==240&&id==frame.sequence&&cap==(uint32_t)(frame.capture_us/1000u)&&now==(uint32_t)(clock_us/1000u));
 *r=injected;r->frame_id=id;r->capture_ms=cap;++processes;clock_us+=13000;return 0;
}
static void next(unsigned id,unsigned status,unsigned n){
 memset(&injected,0,sizeof injected);injected.status=(uint8_t)status;injected.count=(uint8_t)n;
 injected.objects[0].bbox=(obstacle_motion_bbox_t){10,20,20,30};
 injected.objects[1].bbox=(obstacle_motion_bbox_t){300,200,20,40};
 injected.objects[0].id=1;injected.objects[1].id=2;
 frame=(traffic_frame_snapshot_t){frame_pixels,320,240,id,id,clock_us-200000};frame_ready=1;
}
int main(void){
 obstacle_service_snapshot_t s;unsigned before;
 magic=0;obstacle_service_init();obstacle_service_poll();obstacle_service_get_snapshot(&s);assert(s.enabled&&!s.available&&!writes&&!processes);
 magic=0x4f423031u;busy=1;obstacle_service_init();assert(!writes);busy=0;obstacle_service_poll();assert(commits==1&&regs[0xac/4]==0x100);
 next(1,OBSTACLE_MOTION_READY,2);obstacle_service_poll();assert(commits==2&&(regs[0xac/4]&0x10f)==0x102&&((regs[0xac/4]>>16)&1023)==213);
 assert(regs[0xb0/4]==(20u|(40u<<16))&&regs[0xb4/4]==(59u|(99u<<16)));
 assert(regs[0xb8/4]==(600u|(400u<<16))&&regs[0xbc/4]==(639u|(479u<<16))&&regs[0xc0/4]==0);
 obstacle_service_get_snapshot(&s);assert(s.count==2&&s.available&&s.proc_ms==13&&s.age_ms==213);
 before=commits;next(1,OBSTACLE_MOTION_DUPLICATE,0);obstacle_service_poll();assert(commits==before); /* no fresh heartbeat */
 busy=1;before=writes;next(2,OBSTACLE_MOTION_READY,2);obstacle_service_poll();assert(writes==before&&frame_ready==0);
 next(3,OBSTACLE_MOTION_READY,1);obstacle_service_poll();assert(writes==before);busy=0;obstacle_service_poll();assert(regs[0xf8/4]==3&&(regs[0xac/4]&15)==1);
 before=commits;clock_us+=600000;obstacle_service_poll();assert(commits==before+1&&(regs[0xac/4]&15)==0);obstacle_service_get_snapshot(&s);assert(s.status==OBSTACLE_MOTION_STALE&&!s.count);
 before=commits;obstacle_service_poll();assert(commits==before);
 next(4,OBSTACLE_MOTION_READY,1);injected.objects[0].bbox.w=0;obstacle_service_poll();assert(!(regs[0xac/4]&15));obstacle_service_get_snapshot(&s);assert(s.status==OBSTACLE_MOTION_INVALID);
 next(5,OBSTACLE_MOTION_READY,9);obstacle_service_poll();assert(!(regs[0xac/4]&15));
 next(6,OBSTACLE_MOTION_READY,1);injected.objects[0].bbox=(obstacle_motion_bbox_t){319,239,2,2};obstacle_service_poll();assert(!(regs[0xac/4]&15));
 next(7,OBSTACLE_MOTION_WARMUP,0);obstacle_service_poll();assert(!(regs[0xac/4]&15));
 next(8,OBSTACLE_MOTION_GLOBAL_CHANGE,0);obstacle_service_poll();assert(!(regs[0xac/4]&15));
 next(9,OBSTACLE_MOTION_READY,1);obstacle_service_poll();assert((regs[0xac/4]&15)==1);obstacle_service_suspend();assert(!(regs[0xac/4]&15));
 obstacle_service_get_snapshot(&s);assert(!s.count&&s.status==OBSTACLE_MOTION_INVALID&&resets>0);obstacle_service_get_snapshot(NULL);
 /* JPEG pause waits for an outstanding mailbox without waiting in a loop,
  * and does not consume/reprocess a new producer frame while paused. */
 busy=1;before=commits;obstacle_service_suspend();assert(!obstacle_service_suspend_ready()&&commits==before);
 next(10,OBSTACLE_MOTION_READY,1);before=processes;obstacle_service_poll();assert(frame_ready&&processes==before);
 busy=0;commit_busy=1;assert(!obstacle_service_suspend_ready()&&!(regs[0xac/4]&15));
 assert(!obstacle_service_suspend_ready());busy=0;commit_busy=0;assert(obstacle_service_suspend_ready());
 obstacle_service_resume();obstacle_service_poll();assert(!frame_ready&&processes==before+1);
 clock_us=0x100000000ull*1000u+1000000;next(10,OBSTACLE_MOTION_READY,1);obstacle_service_poll();obstacle_service_get_snapshot(&s);assert(s.count==1&&s.age_ms==213);
 {
  obstacle_display_snapshot_t display;
  busy=0;commit_busy=0;obstacle_service_init();
  obstacle_service_get_display_snapshot(&display);assert(display.available&&!display.count);
  commit_busy=1;next(20,OBSTACLE_MOTION_READY,2);obstacle_service_poll();
  obstacle_service_get_display_snapshot(&display);assert(!display.count); /* Not ACKed. */
  busy=0;obstacle_service_get_display_snapshot(&display);
  assert(display.count==2&&display.source_frame==20&&display.age_ms==213);
  assert(display.boxes[0].id==1&&display.boxes[0].xmin==20&&display.boxes[0].xmax==59);
  assert(display.boxes[1].xmax==639&&display.boxes[1].ymax==479);
  busy=1;next(21,OBSTACLE_MOTION_READY,1);obstacle_service_poll();
  obstacle_service_get_display_snapshot(&display);assert(display.count==2&&display.source_frame==20);
  busy=0;obstacle_service_poll();obstacle_service_get_display_snapshot(&display);
  assert(display.count==2&&display.source_frame==20); /* New table awaits ACK. */
  busy=0;obstacle_service_get_display_snapshot(&display);assert(display.count==1&&display.source_frame==21);
  clock_us+=600000;obstacle_service_get_display_snapshot(&display);
  assert(!display.count&&display.status==OBSTACLE_MOTION_STALE); /* Expiry without publish. */
  obstacle_service_get_display_snapshot(NULL);busy=0;commit_busy=0;
 }
 /* Manual disable revokes hardware boxes, preserves the user preference
  * through JPEG, and never runs the detector or revives an old ACK on ON. */
 {
  obstacle_display_snapshot_t display;unsigned runs,reset_before;
  clock_us=20000000;frame_ready=0;busy=0;commit_busy=0;obstacle_service_init();
  assert(obstacle_service_is_enabled()==1);
  before=commits;reset_before=resets;
  obstacle_service_set_enabled(1);obstacle_service_set_enabled(2);
  assert(commits==before&&resets==reset_before&&obstacle_service_is_enabled());
  next(30,OBSTACLE_MOTION_READY,2);obstacle_service_poll();
  obstacle_service_get_display_snapshot(&display);assert(display.count==2);
  busy=1;before=commits;obstacle_service_set_enabled(0);
  obstacle_service_get_snapshot(&s);assert(!s.enabled&&!s.count);
  obstacle_service_get_display_snapshot(&display);assert(!display.count&&display.status==OBSTACLE_MOTION_INVALID);
  next(31,OBSTACLE_MOTION_READY,1);runs=processes;obstacle_service_poll();
  assert(frame_ready&&processes==runs&&commits==before);
  busy=0;commit_busy=1;obstacle_service_poll();assert(!(regs[0xac/4]&15));
  before=commits;reset_before=resets;obstacle_service_set_enabled(0);
  assert(commits==before&&resets==reset_before); /* repeated OFF is idempotent */
  obstacle_service_set_enabled(1); /* previous zero table is still in flight */
  busy=0;obstacle_service_get_display_snapshot(&display);assert(!display.count);
  obstacle_service_poll();assert(!frame_ready&&processes==runs); /* old producer frame rejected */
  busy=0;commit_busy=0;next(32,OBSTACLE_MOTION_READY,1);frame.capture_us=clock_us;
  obstacle_service_poll();obstacle_service_get_display_snapshot(&display);
  assert(display.count==1&&processes==runs+1&&display.source_frame==32);
  obstacle_service_suspend();obstacle_service_set_enabled(0);
  obstacle_service_resume();next(33,OBSTACLE_MOTION_READY,2);runs=processes;
  obstacle_service_poll();assert(obstacle_service_is_enabled()==0&&processes==runs);
  obstacle_service_get_display_snapshot(&display);assert(!display.count);
  /* A valid pre-OFF mailbox that ACKs after OFF/ON must not be resurrected. */
  frame_ready=0;busy=0;commit_busy=1;obstacle_service_init();busy=0;
  next(34,OBSTACLE_MOTION_READY,2);obstacle_service_poll();assert(busy);
  obstacle_service_set_enabled(0);obstacle_service_set_enabled(1);busy=0;
  obstacle_service_get_display_snapshot(&display);assert(!display.count);
  obstacle_service_poll();busy=0;obstacle_service_get_display_snapshot(&display);assert(!display.count);
  busy=0;commit_busy=0;magic=0;obstacle_service_init();before=writes;
  obstacle_service_set_enabled(0);obstacle_service_set_enabled(1);obstacle_service_poll();assert(writes==before);
  magic=0x4f423031u;
 }
 puts("MANUAL_DETECTION_SERVICE_TEST_PASS: idempotent ON/OFF, busy clear, no processing while OFF, old frame+ACK rejection, JPEG independence, unavailable hardware");
 puts("OBSTACLE_SERVICE_TEST_PASS: shared-frame once, old hardware, busy no-write/latest-only, 8 bounds/640 mapping, source-age carry+wrap, duplicate no-heartbeat, timeout/invalid/global/warmup/JPEG clear");return 0;
}
