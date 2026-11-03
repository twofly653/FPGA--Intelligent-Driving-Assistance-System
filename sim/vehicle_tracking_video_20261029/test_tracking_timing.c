/* Host-only public-API timing and evidence tests. No hardware access. */
#include "obstacle_motion_core.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define W OBSTACLE_MOTION_INPUT_WIDTH
#define H OBSTACLE_MOTION_INPUT_HEIGHT
static obstacle_motion_t state;
static obstacle_motion_result_t result;
static uint16_t pixels[W*H];
static uint32_t frame_id,capture_ms;
static unsigned checks,failures,cases;
static const char *case_name;
static void check(int ok,const char *what){++checks;if(!ok){++failures;printf("FAIL %s: %s (frame=%lu ms=%lu status=%u count=%u)\n",case_name,what,(unsigned long)frame_id,(unsigned long)capture_ms,result.status,result.count);}}
static uint16_t rgb(unsigned r,unsigned g,unsigned b){return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
static void scene(void){unsigned x,y;for(y=0;y<H;++y)for(x=0;x<W;++x){unsigned g=64u+3u*((x/40u+y/40u)%3u);pixels[y*W+x]=rgb(g,g,g);}}
static void rectangle(unsigned x0,unsigned y0,unsigned w,unsigned h,uint16_t c){unsigned x,y;for(y=y0;y<y0+h&&y<H;++y)for(x=x0;x<x0+w&&x<W;++x)pixels[y*W+x]=c;}
static void target(unsigned x0,unsigned style){unsigned x,y;for(y=70u;y<118u;++y)for(x=x0;x<x0+40u&&x<W;++x){unsigned t=((x-x0)/10u+(y-70u)/12u)%2u;pixels[y*W+x]=style?rgb(25u+8u*t,65u+8u*t,215u+8u*t):rgb(210u+12u*t,125u+12u*t,35u+12u*t);}}
static void step(unsigned elapsed){unsigned i,j;++frame_id;capture_ms+=elapsed;
 check(obstacle_motion_process(&state,pixels,W,H,frame_id,capture_ms,capture_ms,&result)==0,"process succeeds");check(result.count<=8u,"bounded count");
 for(i=0;i<result.count;++i){const obstacle_motion_object_t *o=&result.objects[i];check(o->id&&o->bbox.w&&o->bbox.h&&(unsigned)o->bbox.x+o->bbox.w<=W&&(unsigned)o->bbox.y+o->bbox.h<=H,"bounded nonempty box");for(j=0;j<i;++j)check(o->id!=result.objects[j].id,"unique ID");}}
static void begin(const char *name,uint32_t ms,uint32_t id){unsigned i;case_name=name;++cases;printf("CASE %s\n",name);obstacle_motion_init(&state);memset(&result,0,sizeof result);capture_ms=ms;frame_id=id;for(i=0;i<4u;++i){scene();step(100u);check(result.status==(i<3u?OBSTACLE_MOTION_WARMUP:OBSTACLE_MOTION_READY),"warmup contract");check(!result.count,"empty background");}}
static unsigned iou(const obstacle_motion_bbox_t *b,unsigned x,unsigned y,unsigned w,unsigned h){unsigned xa=b->x>x?b->x:x,ya=b->y>y?b->y:y;unsigned xb=(unsigned)b->x+b->w<x+w?(unsigned)b->x+b->w:x+w;unsigned yb=(unsigned)b->y+b->h<y+h?(unsigned)b->y+b->h:y+h;unsigned a=xb>xa&&yb>ya?(xb-xa)*(yb-ya):0u,total=(unsigned)b->w*b->h+w*h-a;return total?100u*a/total:0u;}
static void observed(uint16_t id,unsigned x,const char *why){check(result.status==OBSTACLE_MOTION_READY,"ready observation");check(result.count==1u,why);if(result.count==1u){check(result.objects[0].id==id,"same observed ID");check(iou(&result.objects[0].bbox,x,70u,40u,48u)>=55u,"current target coverage");}}
static uint16_t train(unsigned *x){unsigned i;uint16_t id=0u;for(i=0;i<6u;++i){*x=20u+i*6u;scene();target(*x,0u);step(100u);if(i<2u)check(!result.count,"admission evidence");else{check(result.count==1u,"confirmed target");if(result.count==1u){if(!id)id=result.objects[0].id;observed(id,*x,"training visible");}}}check(id!=0u,"training ID");return id;}
static void mixed(void){static const unsigned dt[]={100u,200u,400u,100u,400u,200u,100u,200u,400u};unsigned i,e=0u,x;uint16_t id;begin("mixed 100/200/400ms",1000u,0u);id=train(&x);for(i=0;i<sizeof dt/sizeof dt[0];++i){e+=dt[i];x=50u+60u*e/1000u;scene();target(x,0u);step(dt[i]);observed(id,x,"mixed-cadence visible");}}
static void short_loss(void){unsigned x;uint16_t id;begin("600ms reacquisition",4000u,100u);id=train(&x);scene();step(200u);check(!result.count,"absent clears overlay");x+=36u;scene();target(x,0u);step(400u);observed(id,x,"no new admission after short loss");}
static void wrap(void){static const unsigned dt[]={100u,200u,400u,100u};unsigned i,e=0u,x;uint16_t id;begin("time and sequence wrap",UINT32_MAX-900u,UINT32_MAX-10u);id=train(&x);for(i=0;i<sizeof dt/sizeof dt[0];++i){e+=dt[i];x=50u+60u*e/1000u;scene();target(x,0u);step(dt[i]);observed(id,x,"wrap observed");}check(capture_ms<2000u&&frame_id<20u,"both clocks crossed");}
static void exit_clears(void){static const unsigned dt[]={100u,200u,400u,100u,200u};unsigned i,x;begin("departure no predicted overlay",8000u,200u);(void)train(&x);for(i=0;i<sizeof dt/sizeof dt[0];++i){scene();step(dt[i]);check(!result.count,"absence zero boxes");}}
static void weak_and_foreign(void){unsigned x;uint16_t id;begin("weak and foreign observations",12000u,300u);id=train(&x);scene();rectangle(x+40u,70u,6u,48u,rgb(216u,131u,41u));step(100u);check(!result.count,"fragment cannot render full template");x+=12u;scene();target(x,0u);step(100u);observed(id,x,"recover weak evidence");x+=6u;scene();target(x,1u);step(100u);check(!result.count,"one foreign observation tentative");x+=6u;scene();target(x,1u);step(100u);check(!result.count,"two foreign observations tentative");x+=6u;scene();target(x,0u);step(100u);observed(id,x,"original appearance original ID");}
int main(void){mixed();short_loss();wrap();exit_clears();weak_and_foreign();printf("RESULT cases=%u checks=%u failures=%u\n",cases,checks,failures);return failures?1:0;}
