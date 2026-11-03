#include "obstacle_service.h"
#include "app_features.h"
#include <string.h>
#if APP_ENABLE_OBSTACLES
#include "obstacle_motion_core.h"
#include "traffic_service.h"
#ifdef OBSTACLE_SERVICE_HOST_TEST
extern uint32_t obstacle_test_read(unsigned offset);
extern void obstacle_test_write(unsigned offset,uint32_t value);
extern uint64_t obstacle_test_now_us(void);
#define read_reg obstacle_test_read
#define write_reg obstacle_test_write
#define now_us obstacle_test_now_us
#else
#include "pl_hub.h"
#include "xtime_l.h"
static uint32_t read_reg(unsigned o){return pl_rd(o);}
static void write_reg(unsigned o,uint32_t v){pl_wr(o,v);}
static uint64_t now_us(void){XTime t;XTime_GetTime(&t);
 return (uint64_t)(t/COUNTS_PER_SECOND)*1000000u+
        (uint64_t)((t%COUNTS_PER_SECOND)*1000000u/COUNTS_PER_SECOND);}
#endif
#define COMMIT_REG 0xacu
#define BOX_REG 0xb0u
#define STATUS_REG 0xf0u
#define MAGIC_REG 0xf4u
#define SOURCE_REG 0xf8u
#define SEQUENCE_REG 0xfcu
#define SOURCE_MAX_AGE_US 500000u
static obstacle_motion_t motion;
static obstacle_motion_result_t current;
static obstacle_service_snapshot_t stats;
static uint64_t capture_us;
static unsigned have_current,pending,paused;
static unsigned overlay_labels;
static uint8_t remote_classes[8];
static obstacle_display_snapshot_t display_current,display_inflight;
static uint64_t display_capture_us,inflight_capture_us;
static unsigned display_have,inflight_have,mailbox_inflight;
static uint32_t control_epoch,inflight_epoch;
static uint64_t enable_after_us;
static void accept_ack(uint32_t status){
 if(mailbox_inflight&&!(status&1u)){
  /* A mailbox sent before a manual state change must never revive old boxes. */
  if(inflight_epoch==control_epoch){
   display_current=display_inflight;display_capture_us=inflight_capture_us;
   display_have=inflight_have;
  }
  mailbox_inflight=0;
 }
}
#if defined(OBSTACLE_CAPTURE_DEBUG) && !defined(OBSTACLE_SERVICE_HOST_TEST)
#include "xil_cache.h"
#define OB_DEBUG_FRAMES 20u
/* Immutable actual producer frames, flushed for non-coherent APU reads.
 * Diagnostic ELF only. Capture does not rearm the camera or stop the CPU. */
uint16_t obstacle_capture_frames[OB_DEBUG_FRAMES][320u*240u] __attribute__((aligned(32)));
volatile uint32_t obstacle_capture_meta[OB_DEBUG_FRAMES][16] __attribute__((aligned(32)));
volatile uint32_t obstacle_capture_boxes[OB_DEBUG_FRAMES][8][8] __attribute__((aligned(32)));
volatile uint32_t obstacle_capture_count[8] __attribute__((aligned(32)));
obstacle_motion_t obstacle_capture_initial_state __attribute__((aligned(32)));
void obstacle_service_debug_begin(void){
 memcpy(&obstacle_capture_initial_state,&motion,sizeof motion);
 Xil_DCacheFlushRange((INTPTR)&obstacle_capture_initial_state,sizeof motion);
 obstacle_capture_count[0]=0;++obstacle_capture_count[2];
 Xil_DCacheFlushRange((INTPTR)obstacle_capture_count,sizeof obstacle_capture_count);
}
static void retain_obstacle_input(const traffic_frame_snapshot_t *f,const obstacle_motion_result_t *r,uint64_t processed_us){
 unsigned slot=obstacle_capture_count[0],i;
 if(slot>=OB_DEBUG_FRAMES)return;
 memcpy(obstacle_capture_frames[slot],f->pixels,sizeof obstacle_capture_frames[slot]);
 obstacle_capture_meta[slot][0]=f->sequence;obstacle_capture_meta[slot][1]=f->source_frame;
 obstacle_capture_meta[slot][2]=(uint32_t)(f->capture_us/1000u);obstacle_capture_meta[slot][3]=(uint32_t)(processed_us/1000u);
 obstacle_capture_meta[slot][4]=r->status;obstacle_capture_meta[slot][5]=r->count;
 obstacle_capture_meta[slot][6]=r->changed_pixels;obstacle_capture_meta[slot][7]=r->foreground_pixels;
 obstacle_capture_meta[slot][8]=r->threshold;obstacle_capture_meta[slot][9]=r->background_threshold;
 obstacle_capture_meta[slot][10]=(uint32_t)(int32_t)r->illumination_delta;
 for(i=0;i<8u;++i){const obstacle_motion_object_t *o=&r->objects[i];
  obstacle_capture_boxes[slot][i][0]=o->bbox.x;obstacle_capture_boxes[slot][i][1]=o->bbox.y;
  obstacle_capture_boxes[slot][i][2]=o->bbox.w;obstacle_capture_boxes[slot][i][3]=o->bbox.h;
  obstacle_capture_boxes[slot][i][4]=o->id;obstacle_capture_boxes[slot][i][5]=o->area;
  obstacle_capture_boxes[slot][i][6]=o->motion_pixels;obstacle_capture_boxes[slot][i][7]=o->confidence;
 }
 Xil_DCacheFlushRange((INTPTR)obstacle_capture_frames[slot],sizeof obstacle_capture_frames[slot]);
 Xil_DCacheFlushRange((INTPTR)obstacle_capture_meta[slot],sizeof obstacle_capture_meta[slot]);
 Xil_DCacheFlushRange((INTPTR)obstacle_capture_boxes[slot],sizeof obstacle_capture_boxes[slot]);
 obstacle_capture_count[0]=slot+1u;obstacle_capture_count[1]=0x4f424432u;
 Xil_DCacheFlushRange((INTPTR)obstacle_capture_count,sizeof obstacle_capture_count);
}
#endif

static uint32_t pack_xy(unsigned x,unsigned y){return x|(y<<16);}
static void clear_current(unsigned status){
 obstacle_motion_reset(&motion);
 memset(&current,0,sizeof current);current.status=(uint8_t)status;
 stats.status=status;stats.count=0;have_current=0;pending=1;++stats.sequence;
}
static void publish(void){
 unsigned i,n=current.count;uint64_t t=now_us(),age=have_current&&t>=capture_us?t-capture_us:0;
 uint32_t mailbox_status=read_reg(STATUS_REG);
 accept_ack(mailbox_status);
 if(!pending)return;
 if(have_current&&(t<capture_us||age>SOURCE_MAX_AGE_US)){clear_current(OBSTACLE_MOTION_STALE);n=0;age=0;}
 if(mailbox_status&1u){++stats.busy_deferred;return;}
 if(n>OBSTACLE_MOTION_MAX_OBJECTS){clear_current(OBSTACLE_MOTION_INVALID);n=0;}
 for(i=0;i<n;++i){obstacle_motion_bbox_t b=current.objects[i].bbox;
  if(!b.w||!b.h||b.x>=320u||b.y>=240u||(unsigned)b.x+b.w>320u||(unsigned)b.y+b.h>240u){
   clear_current(OBSTACLE_MOTION_INVALID);n=0;break;
  }
 }
 /* Staging is never written while a prior mailbox is busy. Commit alone
  * snapshots all 16 words and metadata, and the PL holds them until ACK. */
 for(i=0;i<OBSTACLE_MOTION_MAX_OBJECTS;++i){uint32_t a=0,b=0;
  if(i<n){obstacle_motion_bbox_t q=current.objects[i].bbox;
   a=pack_xy(2u*q.x,2u*q.y);
   b=pack_xy(2u*((unsigned)q.x+q.w)-1u,2u*((unsigned)q.y+q.h)-1u);
#if APP_ENABLE_PC_NETWORK
   if(overlay_labels){unsigned id=current.objects[i].id,cls=remote_classes[i];
    a|=(id&63u)<<10;a|=((id>>6)&63u)<<26;
    b|=((id>>12)&15u)<<10;b|=(cls&3u)<<14;b|=((cls>>2)&63u)<<26;
   }
#endif
  }
  write_reg(BOX_REG+8u*i,a);write_reg(BOX_REG+8u*i+4u,b);
 }
 /* Carry source age, not a fresh timestamp: the pixel-domain TTL includes
  * capture/copy/analysis and the wait until the next vertical blank. */
 age=n&&have_current?(now_us()-capture_us+999u)/1000u:0;
 if(age>500u){clear_current(OBSTACLE_MOTION_STALE);n=0;age=0;}
 write_reg(SOURCE_REG,stats.source_frame);write_reg(SEQUENCE_REG,stats.sequence);
 memset(&display_inflight,0,sizeof display_inflight);
 display_inflight.sequence=stats.sequence;display_inflight.source_frame=stats.source_frame;
 display_inflight.available=stats.available;display_inflight.status=current.status;
 display_inflight.proc_ms=stats.proc_ms;display_inflight.count=n;
 for(i=0;i<n;++i){obstacle_motion_object_t *o=&current.objects[i];obstacle_display_box_t *d=&display_inflight.boxes[i];
  d->id=o->id;d->xmin=(uint16_t)(2u*o->bbox.x);d->ymin=(uint16_t)(2u*o->bbox.y);
  d->xmax=(uint16_t)(2u*((unsigned)o->bbox.x+o->bbox.w)-1u);
  d->ymax=(uint16_t)(2u*((unsigned)o->bbox.y+o->bbox.h)-1u);
 }
 inflight_capture_us=capture_us;inflight_have=have_current;mailbox_inflight=1;inflight_epoch=control_epoch;
 write_reg(COMMIT_REG,(1u<<8)|n|((uint32_t)age<<16)|((APP_ENABLE_PC_NETWORK&&overlay_labels)?(1u<<9):0u));
 ++stats.commits;pending=0;
}
void obstacle_service_init(void){
 memset(&stats,0,sizeof stats);memset(&current,0,sizeof current);
 memset(&display_current,0,sizeof display_inflight);memset(&display_inflight,0,sizeof display_inflight);
 display_capture_us=inflight_capture_us=0;display_have=inflight_have=mailbox_inflight=0;
 control_epoch=inflight_epoch=0;enable_after_us=0;
 obstacle_motion_init(&motion);have_current=pending=paused=0;capture_us=0;
 #if defined(OBSTACLE_CAPTURE_DEBUG) && !defined(OBSTACLE_SERVICE_HOST_TEST)
 memset((void*)obstacle_capture_count,0,sizeof obstacle_capture_count);
 Xil_DCacheFlushRange((INTPTR)obstacle_capture_count,sizeof obstacle_capture_count);
 obstacle_service_debug_begin();
#endif
 stats.enabled=1;overlay_labels=read_reg(MAGIC_REG)==0x4f423032u;
 stats.available=overlay_labels||read_reg(MAGIC_REG)==0x4f423031u;
 stats.status=OBSTACLE_MOTION_WARMUP;
 if(stats.available){pending=1;++stats.sequence;publish();}
}
unsigned obstacle_service_is_enabled(void){return stats.enabled;}
unsigned obstacle_service_is_paused(void){return paused;}
void obstacle_service_remote_clear(void){clear_current(OBSTACLE_MOTION_STALE);}
void obstacle_service_remote_apply(const pn_box_t *boxes,unsigned count,uint32_t frame,uint64_t source_us,unsigned proc){
 unsigned i;
 if(!stats.enabled||paused||count>8u||!boxes||source_us<enable_after_us)return;
 memset(&current,0,sizeof current);current.status=OBSTACLE_MOTION_READY;current.count=(uint8_t)count;
 for(i=0;i<count;++i){obstacle_motion_object_t*o=&current.objects[i];const pn_box_t*b=&boxes[i];
  o->id=(uint16_t)b->id;o->bbox.x=b->x1;o->bbox.y=b->y1;o->bbox.w=b->x2-b->x1+1u;o->bbox.h=b->y2-b->y1+1u;
  remote_classes[i]=b->cls;
 }
 capture_us=source_us;have_current=1;stats.source_frame=frame;stats.status=0;stats.count=count;stats.proc_ms=proc;
 ++stats.processed;++stats.sequence;pending=1;
}
void obstacle_service_set_enabled(unsigned enabled){
 if(enabled>1u||stats.enabled==enabled)return;
 stats.enabled=enabled;++control_epoch;
 enable_after_us=enabled?now_us():0;
 display_have=0;memset(&display_current,0,sizeof display_current);
 display_current.status=enabled?OBSTACLE_MOTION_WARMUP:OBSTACLE_MOTION_INVALID;
 /* Queue a zero-count table even behind a busy mailbox. No waiting or full
  * camera reset: RGB, traffic recognition and sensor services continue. */
 clear_current(enabled?OBSTACLE_MOTION_WARMUP:OBSTACLE_MOTION_INVALID);
 if(stats.available)publish();
}
void obstacle_service_suspend(void){
 paused=1;if(stats.available){clear_current(OBSTACLE_MOTION_INVALID);publish();}
}
int obstacle_service_suspend_ready(void){
 if(!stats.available)return 1;
 publish();return !pending&&!(read_reg(STATUS_REG)&1u);
}
void obstacle_service_resume(void){
 paused=0;
#if APP_ENABLE_PC_NETWORK
 enable_after_us=now_us();
#endif
}
void obstacle_service_poll(void){
#if APP_ENABLE_PC_NETWORK
 if(!stats.available)return;
 if(have_current&&now_us()-capture_us>SOURCE_MAX_AGE_US)clear_current(OBSTACLE_MOTION_STALE);
 publish();return;
#else
 traffic_frame_snapshot_t frame;uint64_t t=now_us();
 if(!stats.available)return;
 if(paused||!stats.enabled){publish();return;}
 if(have_current&&(t<capture_us||t-capture_us>SOURCE_MAX_AGE_US))clear_current(OBSTACLE_MOTION_STALE);
 if(traffic_service_take_frame(&frame)){
  /* A producer frame retained across OFF/ON is not a new background sample. */
  if(enable_after_us&&frame.capture_us<enable_after_us){publish();return;}
  obstacle_motion_result_t next;uint64_t begin=now_us(),end;
  (void)obstacle_motion_process(&motion,frame.pixels,frame.width,frame.height,
      frame.sequence,(uint32_t)(frame.capture_us/1000u),(uint32_t)(begin/1000u),&next);
  end=now_us();
#if defined(OBSTACLE_CAPTURE_DEBUG) && !defined(OBSTACLE_SERVICE_HOST_TEST)
  retain_obstacle_input(&frame,&next,end);
#endif
  stats.proc_ms=(uint32_t)((end-begin+999u)/1000u);++stats.processed;
  if(next.status!=OBSTACLE_MOTION_DUPLICATE){
   current=next;capture_us=frame.capture_us;have_current=1;
   stats.source_frame=frame.source_frame;stats.status=next.status;stats.count=next.count;
   pending=1;++stats.sequence;
   if(next.status==OBSTACLE_MOTION_STALE||next.status==OBSTACLE_MOTION_INVALID)
    clear_current(next.status);
  }
 }
 publish();
#endif
}
void obstacle_service_get_display_snapshot(obstacle_display_snapshot_t *out){
 uint64_t t,age;
 if(!out)return;
 if(!stats.available){memset(out,0,sizeof *out);out->status=OBSTACLE_MOTION_INVALID;out->age_ms=1001u;return;}
 accept_ack(read_reg(STATUS_REG));
 *out=display_current;out->available=stats.available;
 if(!stats.enabled){out->count=0;out->status=OBSTACLE_MOTION_INVALID;out->age_ms=1001u;return;}
 t=now_us();age=display_have&&t>=display_capture_us?(t-display_capture_us)/1000u:1001u;
 out->age_ms=(uint32_t)(age>0x7fffffffu?0x7fffffffu:age);
 if(!display_have||age>500u){out->count=0;if(out->status==OBSTACLE_MOTION_READY)out->status=OBSTACLE_MOTION_STALE;}
}
void obstacle_service_get_snapshot(obstacle_service_snapshot_t *out){
 uint64_t t=now_us(),age=have_current&&t>=capture_us?(t-capture_us)/1000u:1001u;
 if(!out)return;
 *out=stats;out->age_ms=(uint32_t)(age>0xffffffffu?0xffffffffu:age);
 if(!stats.available||!have_current||age>500u)out->count=0;
}
#else
void obstacle_service_get_display_snapshot(obstacle_display_snapshot_t *out){if(out){memset(out,0,sizeof *out);out->status=5u;out->age_ms=1001u;}}
void obstacle_service_set_enabled(unsigned enabled){(void)enabled;}
unsigned obstacle_service_is_enabled(void){return 0;}
unsigned obstacle_service_is_paused(void){return 1;}
void obstacle_service_remote_clear(void){}
void obstacle_service_remote_apply(const pn_box_t*b,unsigned n,uint32_t f,uint64_t t,unsigned p){(void)b;(void)n;(void)f;(void)t;(void)p;}
void obstacle_service_init(void){}
void obstacle_service_poll(void){}
void obstacle_service_suspend(void){}
int obstacle_service_suspend_ready(void){return 1;}
void obstacle_service_resume(void){}
void obstacle_service_get_snapshot(obstacle_service_snapshot_t *out){if(out){memset(out,0,sizeof *out);out->age_ms=1001u;}}
#endif
