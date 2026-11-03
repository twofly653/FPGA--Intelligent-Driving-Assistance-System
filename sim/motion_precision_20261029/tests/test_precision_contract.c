
#include "obstacle_motion_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static obstacle_motion_t s,saved;
static obstacle_motion_result_t r;
static uint16_t f[320u*240u];
static uint32_t seq,ms;
static void draw(unsigned x){unsigned i,j;memset(f,0,sizeof f);for(i=70;i<110;++i)for(j=x;j<x+32;++j)f[i*320+j]=0xffff;}
static void run(void){++seq;ms+=200;assert(!obstacle_motion_process(&s,f,320,240,seq,ms,ms,&r));assert(r.count<=8);}
static void start(void){unsigned t;obstacle_motion_init(&s);seq=0;ms=1000;memset(f,0,sizeof f);for(t=0;t<4;++t){run();assert(!r.count);}}
int main(void){unsigned t;uint16_t id;clock_t begin;start();
 draw(20);run();assert(!r.count);saved=s;
 assert(!obstacle_motion_process(&s,f,320,240,seq,ms,ms,&r)&&r.status==OBSTACLE_MOTION_DUPLICATE&&!r.count);
 assert(!memcmp(&saved,&s,sizeof s));
 draw(26);run();assert(!r.count);draw(32);run();assert(r.count==1);id=r.objects[0].id;
 /* Retain a previously confirmed stationary object using current pixels. */
 for(t=0;t<30;++t){run();assert(r.count==1&&r.objects[0].id==id);}
 draw(38);run();assert(r.count==1&&r.objects[0].id==id);
 memset(f,0,sizeof f);for(t=0;t<12;++t){run();assert(!r.count);}
 assert(!obstacle_motion_process(&s,f,320,240,seq,ms,ms+501,&r)&&r.status==OBSTACLE_MOTION_STALE&&!r.count&&!s.have_previous);
 run();assert(r.status==OBSTACLE_MOTION_WARMUP);ms+=800;run();assert(r.status==OBSTACLE_MOTION_STALE&&!r.count);
 assert(obstacle_motion_process(&s,NULL,320,240,1,1,1,&r)==-1&&r.status==OBSTACLE_MOTION_INVALID);
 assert(obstacle_motion_process(&s,f,160,120,1,1,1,&r)==-1);
 assert(obstacle_motion_process(NULL,f,320,240,1,1,1,&r)==-1);
 assert(obstacle_motion_process(&s,f,320,240,1,1,1,NULL)==-1);
 obstacle_motion_init(&s);seq=0xfffffffbu;ms=0xfffffd00u;memset(f,0,sizeof f);
 for(t=0;t<5;++t){run();assert(!r.count);}draw(20);run();draw(26);run();draw(32);run();assert(r.count==1);
 start();draw(20);run();memset(f,0,sizeof f);run();draw(26);run();draw(32);run();assert(!r.count);draw(38);run();assert(r.count==1);
 /* Large luma cut invalidates all tracks, then learns two clean frames. */
 for(t=0;t<320u*240u;++t){f[t]=0xffff;}
 run();assert(!r.count&&r.status==OBSTACLE_MOTION_GLOBAL_CHANGE);
 start();begin=clock();for(t=0;t<400;++t){draw(10+(t%40)*6);run();}
 printf("PRECISION_CONTRACT_PASS mean_host_ms=%.3f state_bytes=%lu (host only, includes rendering)\n",1000.0*(clock()-begin)/CLOCKS_PER_SEC/400,(unsigned long)sizeof s);
 return 0;
}
