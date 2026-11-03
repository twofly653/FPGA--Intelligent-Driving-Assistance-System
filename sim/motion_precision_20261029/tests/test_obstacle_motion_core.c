#include "obstacle_motion_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Current contract: admission needs >=3 consecutive moving observations;
 * confirmed objects remain while present pixels support their silhouette.
 * Earlier single-frame admission / immediate stop removal was superseded. */

static obstacle_motion_t state;
static obstacle_motion_result_t result;
static uint16_t frame[320u*240u];
static uint32_t sequence, milliseconds;
static unsigned checks;

#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"FAIL line %d: %s (status=%u count=%u changed=%u fg=%u thr=%u)\n",__LINE__,#c,result.status,result.count,result.changed_pixels,result.foreground_pixels,result.threshold); exit(1); } } while(0)

static uint16_t gray(unsigned x)
{
    if(x>255u)x=255u;
    return (uint16_t)(((x>>3)<<11)|((x>>2)<<5)|(x>>3));
}

static void scene(unsigned level)
{
    unsigned x,y;
    for(y=0;y<240u;++y) for(x=0;x<320u;++x)
        frame[y*320u+x]=gray(level+((x/40u+y/40u)%3u)*3u);
}

static void textured(unsigned dx, unsigned dy)
{
    unsigned x,y;
    for(y=0;y<240u;++y) for(x=0;x<320u;++x)
        frame[y*320u+x]=gray(((x+dx)/8u+(y+dy)/8u)%2u?185u:45u);
}

static void rect(int x,int y,int w,int h,unsigned level)
{
    int xx,yy;
    for(yy=y;yy<y+h;++yy) for(xx=x;xx<x+w;++xx)
        if(xx>=0 && xx<320 && yy>=0 && yy<240)frame[(unsigned)yy*320u+(unsigned)xx]=gray(level);
}

static void run(void)
{
    ++sequence; milliseconds+=100u;
    CHECK(obstacle_motion_process(&state,frame,320,240,sequence,milliseconds,milliseconds,&result)==0);
    CHECK(result.count<=OBSTACLE_MOTION_MAX_OBJECTS);
    {
        unsigned i,j;
        for(i=0;i<result.count;++i) {
            const obstacle_motion_object_t *o=&result.objects[i];
            CHECK(o->bbox.w>0 && o->bbox.h>0 && o->bbox.x+o->bbox.w<=320u && o->bbox.y+o->bbox.h<=240u);
            CHECK(o->area>=40u && o->confidence<=100u && o->id!=0u);
            for(j=0;j<i;++j)CHECK(o->id!=result.objects[j].id);
        }
    }
}

static void start(void)
{
    unsigned i;
    obstacle_motion_init(&state);sequence=0;milliseconds=1000;
    for(i=0;i<3u;++i) { scene(64);run();CHECK(result.status==OBSTACLE_MOTION_WARMUP && result.count==0); }
    scene(64);run();CHECK(result.status==OBSTACLE_MOTION_READY && result.count==0);
}

static unsigned overlap(const obstacle_motion_bbox_t *b,int x,int y,int w,int h)
{
    int xa=b->x>x?b->x:x, ya=b->y>y?b->y:y;
    int xb=(int)b->x+b->w<x+w?(int)b->x+b->w:x+w;
    int yb=(int)b->y+b->h<y+h?(int)b->y+b->h:y+h;
    unsigned inter=xb>xa && yb>ya?(unsigned)((xb-xa)*(yb-ya)):0u;
    unsigned uni=(unsigned)b->w*b->h+(unsigned)(w*h)-inter;
    return uni?inter*100u/uni:0u;
}

static const obstacle_motion_object_t *at(int x,int y,int w,int h)
{
    unsigned i;
    for(i=0;i<result.count;++i) if(overlap(&result.objects[i].bbox,x,y,w,h)>=65u)return &result.objects[i];
    return NULL;
}

static void single_and_stop(void)
{
    unsigned t;uint16_t id=0;
    start();
    for(t=0;t<18u;++t) {
        const obstacle_motion_object_t *o;
        scene(64);rect(12+(int)t*6,60,32,44,210);run();
        if(t<2u){CHECK(result.count==0);continue;}
        CHECK(result.status==OBSTACLE_MOTION_READY && result.count==1u);
        o=at(12+(int)t*6,60,32,44);CHECK(o!=NULL);
        if(!id)id=o->id;CHECK(o->id==id);
        /* The box follows the present silhouette, not the union of old trails. */
        CHECK(result.objects[0].bbox.w<=38u);
    }
    scene(64);rect(114,60,32,44,210);run();CHECK(result.count==1 && result.objects[0].id==id);
    scene(64);rect(114,60,32,44,210);run();CHECK(result.count==1 && result.objects[0].id==id);
    scene(64);rect(120,60,32,44,210);run();CHECK(result.count==1 && result.objects[0].id==id);
    scene(64);run();CHECK(result.count==0);
    for(t=0;t<6u;++t){scene(64);run();CHECK(result.count==0);}
}

static void multiple_and_neighbours(void)
{
    unsigned t;uint16_t a=0,b=0;
    start();
    for(t=0;t<20u;++t) {
        const obstacle_motion_object_t *oa,*ob;
        int ax=20+(int)t*4,bx=252-(int)t*3;
        scene(64);rect(ax,35,34,42,210);rect(bx,142,30,36,165);run();
        if(t<2u){CHECK(result.count==0);continue;}
        CHECK(result.count==2);
        oa=at(ax,35,34,42);ob=at(bx,142,30,36);CHECK(oa && ob);
        if(!a){a=oa->id;b=ob->id;}CHECK(oa->id==a && ob->id==b && a!=b);
    }
    scene(64);rect(100,35,34,42,210);run();CHECK(result.count==1 && result.objects[0].id==a);
    scene(64);run();CHECK(result.count==0);
    start();
    for(t=0;t<12u;++t) {
        const obstacle_motion_object_t *oa,*ob;
        int x=40+(int)t*4;
        scene(64);rect(x,90,30,40,210);rect(x+40,90,30,40,185);run();
        if(t<2u){CHECK(result.count==0);continue;}
        CHECK(result.count==2);
        oa=at(x,90,30,40);ob=at(x+40,90,30,40);CHECK(oa && ob);
        CHECK(oa->bbox.x+oa->bbox.w<ob->bbox.x);
    }
}

static void eight_and_edges(void)
{
    unsigned t,i;
    start();
    for(t=0;t<8u;++t) {
        scene(64);
        for(i=0;i<8u;++i)rect(10+(int)(i%4u)*72+(int)t*2,18+(int)(i/4u)*110,20,28,200);
        run();
        if(t<3u){CHECK(result.count==0);continue;}
        CHECK(result.count==8);
        for(i=0;i<8u;++i)CHECK(at(10+(int)(i%4u)*72+(int)t*2,18+(int)(i/4u)*110,20,28)!=NULL);
    }
    /* More than the hardware limit remains bounded and returns current boxes. */
    start();
    for(t=0;t<6u;++t){
        scene(64);
        for(i=0;i<9u;++i)rect(8+(int)(i%3u)*95+(int)t*2,10+(int)(i/3u)*75,22,26,210);
        run();if(t>=3u)CHECK(result.count==8);
    }
    start();
    for(t=0;t<3u;++t){scene(64);rect(12-(int)t*6,80,28,40,210);run();}
    scene(64);rect(-6,80,28,40,210);run();CHECK(result.count==1 && result.objects[0].bbox.x==0);
    scene(64);rect(0,80,28,40,210);run();CHECK(result.count==1 && at(0,80,28,40));
}

static void noise_and_light(void)
{
    unsigned t,i;
    uint32_t rng=1234567u;
    start();
    for(t=0;t<12u;++t) {
        scene(64);
        for(i=0;i<320u*240u;++i) {
            int n;
            rng=rng*1664525u+1013904223u;n=(int)((rng>>24)%11u)-5;
            frame[i]=gray((unsigned)(64+n));
        }
        for(i=0;i<32u;++i){rng=rng*1664525u+1013904223u;frame[rng%(320u*240u)]=gray(245);}
        run();CHECK(result.count==0 && result.status==OBSTACLE_MOTION_READY);
    }
    scene(105);run();CHECK(result.count==0 && result.status==OBSTACLE_MOTION_GLOBAL_CHANGE);
    for(t=0;t<3u;++t){scene(105);run();CHECK(result.count==0);}
    /* Small additive drift is compensated rather than called a moving object. */
    for(t=0;t<10u;++t){scene(107u+t*2u);run();CHECK(result.count==0);}
    /* A genuine object can move while exposure drifts slowly. */
    for(t=0;t<8u;++t){scene(127u+t);rect(30+(int)t*4,70,34,40,230);run();
        if(t<2u)CHECK(result.count==0);
        else CHECK(result.count==1 && at(30+(int)t*4,70,34,40));}
    /* A large shot change is not emitted as a full-frame obstacle. */
    for(i=0;i<320u*240u;++i)frame[i]=gray(i%7u?225u:15u);
    run();CHECK(result.count==0 && result.status==OBSTACLE_MOTION_GLOBAL_CHANGE);
}

static void shake_guard(void)
{
    unsigned t;
    obstacle_motion_init(&state);sequence=0;milliseconds=1000;
    for(t=0;t<4u;++t){textured(0,0);run();CHECK(result.count==0);}
    textured(2,0);run();CHECK(result.status==OBSTACLE_MOTION_GLOBAL_CHANGE && result.count==0);
    for(t=0;t<3u;++t){textured(2,0);run();CHECK(result.count==0);}
    textured(4,2);run();CHECK(result.status==OBSTACLE_MOTION_GLOBAL_CHANGE && result.count==0);
}

static void freshness(void)
{
    uint32_t id,stamp;
    unsigned t;
    start();for(t=0;t<3u;++t){scene(64);rect(28+(int)t*6,70,32,40,200);run();}CHECK(result.count==1);
    id=state.previous_frame_id;stamp=state.previous_capture_ms;
    scene(64);rect(90,70,32,40,200);
    CHECK(obstacle_motion_process(&state,frame,320,240,id,stamp,milliseconds+200u,&result)==0);
    CHECK(result.status==OBSTACLE_MOTION_DUPLICATE && result.count==0);
    CHECK(state.previous_frame_id==id && state.previous_capture_ms==stamp);
    CHECK(obstacle_motion_process(&state,frame,320,240,id-1u,stamp,milliseconds+200u,&result)==0);
    CHECK(result.status==OBSTACLE_MOTION_DUPLICATE && result.count==0);
    CHECK(obstacle_motion_process(&state,frame,320,240,id,stamp,milliseconds+501u,&result)==0);
    CHECK(result.status==OBSTACLE_MOTION_STALE && result.count==0 && !state.have_previous);
    run();CHECK(result.status==OBSTACLE_MOTION_WARMUP && result.count==0);
    milliseconds+=800u;run();CHECK(result.status==OBSTACLE_MOTION_STALE && result.count==0);
    CHECK(obstacle_motion_process(&state,NULL,320,240,100,100,100,&result)==-1 && result.status==OBSTACLE_MOTION_INVALID);
    CHECK(obstacle_motion_process(&state,frame,160,120,100,100,100,&result)==-1 && result.status==OBSTACLE_MOTION_INVALID);
    CHECK(obstacle_motion_process(NULL,frame,320,240,100,100,100,&result)==-1);
    CHECK(obstacle_motion_process(&state,frame,320,240,100,100,100,NULL)==-1);
    start();
    CHECK(obstacle_motion_process(&state,frame,320,240,sequence+1u,milliseconds,milliseconds,&result)==0);
    CHECK(result.status==OBSTACLE_MOTION_STALE && result.count==0);
    /* Wrap of both independent generation and millisecond counters is normal. */
    obstacle_motion_init(&state);sequence=0xfffffffbu;milliseconds=0xfffffe0bu;
    for(id=0;id<5u;++id){scene(64);run();CHECK(result.count==0);}
    for(t=0;t<3u;++t){scene(64);rect(28+(int)t*6,70,32,40,210);run();}
    CHECK(result.count==1 && result.status==OBSTACLE_MOTION_READY);
}

static void long_static_no_ghost(void)
{
    unsigned t;
    start();scene(64);rect(60,70,36,44,200);run();CHECK(result.count==0);
    for(t=0;t<600u;++t){scene(64);rect(60,70,36,44,200);run();CHECK(result.count==0);}
    scene(64);run();CHECK(result.count==0);
    scene(64);run();CHECK(result.count==0);
    for(t=0;t<3u;++t){scene(64);rect(168+(int)t*6,70,36,44,200);run();}
    CHECK(result.count==1 && at(180,70,36,44));
}

static void shaded_scene(void)
{
    unsigned x,y;
    for(y=0;y<240u;++y)for(x=0;x<320u;++x)
        frame[y*320u+x]=gray(170u+x/16u+y/24u+((x/13u+y/17u)%2u)*12u);
}

static void disk(int cx,int cy,int radius,unsigned level)
{
    int x,y;
    for(y=cy-radius;y<=cy+radius;++y)for(x=cx-radius;x<=cx+radius;++x)
        if(x>=0 && x<320 && y>=0 && y<240 && (x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius)
            frame[(unsigned)y*320u+(unsigned)x]=gray(level);
}

static void varied_shapes_and_occlusion(void)
{
    unsigned t;uint16_t id=0;
    obstacle_motion_init(&state);sequence=0;milliseconds=1000;
    for(t=0;t<4u;++t){shaded_scene();run();CHECK(result.count==0);}
    for(t=0;t<16u;++t){
        const obstacle_motion_object_t *o;
        shaded_scene();disk(36+(int)t*8,78,18,30);run();
        if(t<2u){CHECK(result.count==0);continue;}
        CHECK(result.count==1);o=at(18+(int)t*8,60,37,37);CHECK(o);
        if(!id)id=o->id;CHECK(o->id==id);
    }
    shaded_scene();run();CHECK(result.count==0);
    /* Multi-tone, nonrectangular silhouette against textured scenery. */
    for(t=0;t<10u;++t){
        int x=15+(int)t*5;
        shaded_scene();disk(x+16,42,10,25);rect(x+2,52,28,32,40);
        rect(x+2,84,10,24,25);rect(x+20,84,10,24,25);run();
        if(t<2u){CHECK(result.count==0);continue;}
        CHECK(result.count==1 && at(x+2,32,29,76));
    }
    shaded_scene();run();CHECK(result.count==0);
    start();
    /* During full overlap there is one observed region, never two prediction
     * boxes. IDs through occlusion are advisory; reacquired split is observed. */
    for(t=0;t<3u;++t){scene(64);rect(48+(int)t*6,80,34,42,210);rect(152-(int)t*6,80,34,42,200);run();}
    CHECK(result.count==2);
    scene(64);rect(78,80,34,42,210);rect(122,80,34,42,200);run();CHECK(result.count==2);
    scene(64);rect(96,80,34,42,210);rect(104,80,34,42,200);run();CHECK(result.count==1);
    scene(64);rect(114,80,34,42,210);rect(86,80,34,42,200);run();CHECK(result.count==1);
    scene(64);rect(132,80,34,42,210);rect(68,80,34,42,200);run();CHECK(result.count==2);
    scene(64);run();CHECK(result.count==0);
}
static uint16_t rgb(unsigned r,unsigned g,unsigned b)
{
    return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}

static void colour_rect(int x,int y,int w,int h,unsigned r,unsigned g,unsigned b)
{
    int xx,yy;
    for(yy=y;yy<y+h;++yy)for(xx=x;xx<x+w;++xx)
        if(xx>=0 && xx<320 && yy>=0 && yy<240)frame[(unsigned)yy*320u+(unsigned)xx]=rgb(r,g,b);
}

static void equal_luma_coloured_targets(void)
{
    unsigned t;uint16_t red_id=0,blue_id=0;
    obstacle_motion_init(&state);sequence=0;milliseconds=1000;
    for(t=0;t<4u;++t){scene(120);run();CHECK(result.count==0);}
    for(t=0;t<16u;++t){
        int rx=24+(int)t*4,bx=240-(int)t*2,k;
        const obstacle_motion_object_t *ro,*bo;
        scene(120);
        /* Both colours have nearly the same luminance as the neutral road.
         * The diagonal darker texture preserves opponent chroma and must not
         * split a single red object into two boxes. */
        colour_rect(rx,130,30,40,210,82,48);
        for(k=4;k<34;++k)colour_rect(rx+3+k*22/34,132+k,2,2,165,37,3);
        colour_rect(bx,55,20,40,42,130,210);
        run();
        if(t<3u){CHECK(result.count<=(t<2u?0u:1u));continue;}
        CHECK(result.count==2);
        ro=at(rx,130,30,40);bo=at(bx,55,20,40);CHECK(ro && bo);
        if(!red_id){red_id=ro->id;blue_id=bo->id;}CHECK(ro->id==red_id && bo->id==blue_id);
    }
    /* Confirmed, currently observed coloured objects survive a stop. */
    run();CHECK(result.count==2);
    scene(120);run();CHECK(result.count==0);
    scene(120);run();CHECK(result.count==0);
}

static void colour_camera_shift(void)
{
    unsigned t,x,y;
    obstacle_motion_init(&state);sequence=0;milliseconds=1000;
    for(t=0;t<5u;++t){
        unsigned dx=t<4u?0u:2u;
        for(y=0;y<240u;++y)for(x=0;x<320u;++x)
            frame[y*320u+x]=(((x+dx)/8u+y/8u)&1u)?rgb(210,82,48):rgb(42,130,210);
        run();
        CHECK(result.count==0);
        if(t==4u)CHECK(result.status==OBSTACLE_MOTION_GLOBAL_CHANGE);
    }
}
static void benchmark(void)
{
    const unsigned frames=200u;
    unsigned t;
    clock_t begin,end;
    start();
    begin=clock();
    for(t=0;t<frames;++t){scene(64);rect(10+(int)(t%36u)*6,70,32,44,210);run();}
    end=clock();
    printf("OBSTACLE_MOTION_BENCHMARK: frames=%u total_ms=%.3f mean_ms=%.3f state_bytes=%lu (host CPU; includes synthetic rendering, NOT ARM or camera latency)\n",frames,1000.0*(double)(end-begin)/CLOCKS_PER_SEC,1000.0*(double)(end-begin)/CLOCKS_PER_SEC/frames,(unsigned long)sizeof(state));
}

int main(void)
{
    single_and_stop();multiple_and_neighbours();eight_and_edges();noise_and_light();shake_guard();freshness();long_static_no_ghost();varied_shapes_and_occlusion();equal_luma_coloured_targets();colour_camera_shift();benchmark();
    printf("OBSTACLE_MOTION_TEST_PASS: %u checks\n",checks);
    return 0;
}


