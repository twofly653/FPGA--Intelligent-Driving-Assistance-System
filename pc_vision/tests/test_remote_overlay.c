#include "obstacle_service.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint32_t regs[64],busy,magic=0x4f423031u;static uint64_t now=1000000;
uint32_t obstacle_test_read(unsigned o){if(o==0xf4)return magic;if(o==0xf0)return busy;assert(0);return 0;}
void obstacle_test_write(unsigned o,uint32_t v){assert(!busy&&o>=0xac&&o<=0xfc);regs[o/4]=v;}
uint64_t obstacle_test_now_us(void){return now;}
int main(void){
 pn_box_t b={17,10,20,29,39,900,2,1};obstacle_display_snapshot_t d;
 obstacle_service_init();obstacle_service_remote_apply(&b,1,9,now-200000,27);obstacle_service_poll();
 obstacle_service_get_display_snapshot(&d);assert(d.count==1&&d.boxes[0].id==17&&d.boxes[0].xmin==20&&d.boxes[0].xmax==59&&d.age_ms==200);
 obstacle_service_set_enabled(0);obstacle_service_remote_apply(&b,1,10,now,27);obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(!d.count);
 obstacle_service_set_enabled(1);obstacle_service_suspend();obstacle_service_remote_apply(&b,1,11,now,27);obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(!d.count);
 obstacle_service_resume();obstacle_service_remote_apply(&b,1,12,now,27);obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(d.count==1);
 now+=10000;obstacle_service_set_enabled(0);obstacle_service_set_enabled(1);
 obstacle_service_remote_apply(&b,1,13,now-1000,27);obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(!d.count);
 obstacle_service_remote_apply(&b,1,14,now,27);obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(d.count==1);
 now+=10000;obstacle_service_suspend();obstacle_service_resume();
 obstacle_service_remote_apply(&b,1,15,now-1000,27);obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(!d.count);
 obstacle_service_remote_apply(&b,1,16,now,27);obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(d.count==1);
 now+=600000;obstacle_service_poll();obstacle_service_get_display_snapshot(&d);assert(!d.count);
 magic=0x4f423032u;b.id=0xbeefu;b.cls=7;obstacle_service_init();
 obstacle_service_remote_apply(&b,1,17,now,27);obstacle_service_poll();
 assert(regs[0xac/4]&(1u<<9));
 {uint32_t a=regs[0xb0/4],z=regs[0xb4/4];
  assert(((a&1023u)|((a>>16)&1023u)<<16)==(20u|(40u<<16)));
  assert((((z>>10)&15u)<<12|((a>>26)&63u)<<6|((a>>10)&63u))==b.id);
  assert((((z>>26)<<2)|((z>>14)&3u))==b.cls);
 }
 puts("REMOTE_OVERLAY_ACK_OFF_PAUSE_TTL_OB02_IDS_PASS");return 0;
}
