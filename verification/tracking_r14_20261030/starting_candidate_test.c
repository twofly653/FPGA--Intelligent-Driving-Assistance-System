#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "obstacle_motion_core.c"
static obstacle_motion_t s;
static obstacle_motion_result_t r;
static uint16_t image[320u*240u];
static uint32_t seq,ms;
static unsigned checks;
#define T(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s status=%u count=%u\n",__LINE__,#c,r.status,r.count);exit(1);}}while(0)
static uint16_t rgb(unsigned v){return (uint16_t)(((v>>3)<<11)|((v>>2)<<5)|(v>>3));}
static void scene(int x,int width){unsigned a,b;for(a=0;a<320u*240u;++a)image[a]=rgb(64);if(width>0)for(b=80;b<120;++b)for(a=(unsigned)x;a<(unsigned)(x+width);++a)image[b*320u+a]=rgb(210);}
static void step(unsigned dt){ms+=dt;++seq;T(obstacle_motion_process(&s,image,320,240,seq,ms,ms,&r)==0);T(r.count<=8);}
static void train(void){unsigned k;obstacle_motion_init(&s);seq=0;ms=1000;for(k=0;k<4;++k){scene(0,0);step(100);}for(k=0;k<8;++k){scene(40+(int)k*4,40);step(100);}T(r.count==1);for(k=0;k<22;++k){scene(68,40);step(100);T(r.count==1);} }
static unsigned slot_for(uint16_t id){unsigned k;for(k=0;k<8;++k)if(s.tracks[k].used&&s.tracks[k].id==id)return k;T(0);return 0;}
static void reference_floor(void){obstacle_motion_component_t out;int photo[6]={0,0,0,256,256,256};unsigned x,y,k;obstacle_motion_track_t *t;obstacle_motion_init(&s);t=&s.tracks[0];t->bbox=(obstacle_motion_bbox_t){50,40,40,40};t->used=t->confirmed=1;t->id=7;t->last_seen_ms=1000;memset(s.smooth,64,sizeof s.smooth);memset(s.chroma,128,sizeof s.chroma);for(y=40;y<80;++y)for(x=50;x<90;++x){s.smooth[y*MW+x]=210;s.foreground[y*MW+x]=1;}sample_appearance(&s,t->bbox,s.appearance[0]);s.appearance_valid[0]=s.reference_valid[0]=1;s.reference_bbox[0]=t->bbox;for(y=40;y<80;++y)for(x=50;x<90;++x)if(x<56||x>=84||y<46||y>=74)s.smooth[y*MW+x]=150;for(k=0;k<8;++k){int ok=continue_track(&s,0,&out,photo,12,10,1100+k*100);T(ok);T(out.bbox.w>=32&&out.bbox.h>=32);t->bbox=out.bbox;t->last_seen_ms=1100+k*100;}T(s.reference_bbox[0].w==40&&s.reference_bbox[0].h==40);}
static void shape_and_prediction(void){obstacle_motion_component_t c;obstacle_motion_track_t *t;obstacle_motion_init(&s);t=&s.tracks[0];t->bbox=(obstacle_motion_bbox_t){50,40,20,20};t->used=t->confirmed=1;t->last_seen_ms=1000;t->chroma_mean[0]=t->chroma_mean[1]=128;memset(&c,0,sizeof c);c.bbox=(obstacle_motion_bbox_t){36,46,48,8};c.area=384;c.motion_pixels=200;c.chroma_mean[0]=c.chroma_mean[1]=128;T(pair_cost(&s,0,&c,1100)==0xffffffffu);c.bbox=(obstacle_motion_bbox_t){56,26,8,48};T(pair_cost(&s,0,&c,1100)==0xffffffffu);c.bbox=(obstacle_motion_bbox_t){52,40,20,20};c.area=400;T(pair_cost(&s,0,&c,1100)!=0xffffffffu);s.velocity_valid[0]=1;s.velocity_pps[0][0]=100;c.bbox.x=60;T(pair_cost(&s,0,&c,1100)==0);}
static void reference_confirmed_only(void){obstacle_motion_track_t *t;int p[6]={0,0,0,256,256,256};obstacle_motion_component_t *c;obstacle_motion_init(&s);t=&s.tracks[0];t->bbox=t->anchor=(obstacle_motion_bbox_t){50,40,20,20};t->used=1;t->id=7;t->hits=1;t->first_seen_ms=t->last_seen_ms=1000;t->chroma_mean[0]=t->chroma_mean[1]=128;s.candidate_count=1;c=&s.candidates[0];c->bbox=(obstacle_motion_bbox_t){52,40,22,20};c->area=440;c->motion_pixels=200;c->chroma_mean[0]=c->chroma_mean[1]=128;memset(&r,0,sizeof r);assign_and_output(&s,&r,1100,p,12,10);T(!t->confirmed&&!s.reference_valid[0]&&r.count==0);c=&s.candidates[0];c->bbox=(obstacle_motion_bbox_t){55,40,24,20};c->area=480;c->motion_pixels=200;memset(&r,0,sizeof r);assign_and_output(&s,&r,1200,p,12,10);T(t->confirmed&&s.reference_valid[0]&&r.count==1);T(s.reference_bbox[0].w==24);}
static void park_miss_and_recover(void){uint8_t saved[3][36];unsigned slot,center=50u*MW+44u,k;uint16_t id,bg;train();id=r.objects[0].id;slot=slot_for(id);bg=s.background_q4[center];T(s.stable[center]==0);memcpy(saved,s.appearance[slot],sizeof saved);memset(s.appearance[slot],255,sizeof saved);scene(68,40);step(100);T(r.count==0);T(s.foreground[center]==1&&s.background_q4[center]==bg);memcpy(s.appearance[slot],saved,sizeof saved);scene(68,40);step(100);T(r.count==1&&r.objects[0].id==id);scene(85,6);step(100);T(r.count==0);scene(68,40);step(100);T(r.count==1&&r.objects[0].id==id);for(k=0;k<8;++k){scene(0,0);step(100);T(r.count==0);} }
static void finite_release(unsigned dt){unsigned slot,center=50u*MW+44u,k;uint32_t start;uint16_t bg;train();slot=slot_for(r.objects[0].id);bg=s.background_q4[center];memset(s.appearance[slot],255,sizeof s.appearance[slot]);start=ms;for(k=0;k<40;++k){scene(68,40);step(dt);T(r.count==0);if(s.background_q4[center]!=bg)break;}T(k<40);T(ms-start>=1184u&&ms-start<=1200u+dt);T(s.foreground[center]==0);}
static void tentative_start(obstacle_motion_bbox_t b)
{
    obstacle_motion_track_t *t;
    obstacle_motion_init(&s);t=&s.tracks[0];
    t->bbox=t->anchor=b;t->used=1;t->id=7;t->hits=1;
    t->first_seen_ms=t->last_seen_ms=1000;
    t->chroma_mean[0]=t->chroma_mean[1]=128;
    memset(s.smooth,160,sizeof s.smooth);memset(s.chroma,128,sizeof s.chroma);
}
static void tentative_observe(obstacle_motion_bbox_t b,uint32_t now)
{
    obstacle_motion_component_t *c=&s.candidates[0];
    s.candidate_count=1;memset(c,0,sizeof *c);c->bbox=b;
    c->area=(uint16_t)((unsigned)b.w*b.h);
    c->motion_pixels=(uint16_t)umax(8u,c->area/3u);
    c->chroma_mean[0]=c->chroma_mean[1]=128;
    memset(&r,0,sizeof r);
    {int p[6]={0,0,0,256,256,256};assign_and_output(&s,&r,now,p,12,10);}
    T(s.tracks[0].used&&s.tracks[0].id==7);
}
static void confirmation_cases(void)
{
    unsigned k;
    tentative_start((obstacle_motion_bbox_t){2,59,15,5});
    for(k=0;k<24u;++k){
        obstacle_motion_bbox_t b={0,59,17,(uint16_t)(7u+k%4u)};
        tentative_observe(b,1100u+100u*k);
        T(!s.tracks[0].confirmed&&r.count==0&&!s.reference_valid[0]);
    }
    tentative_start((obstacle_motion_bbox_t){50,40,20,20});
    tentative_observe((obstacle_motion_bbox_t){49,39,22,22},1100);
    T(!s.tracks[0].confirmed&&r.count==0);
    tentative_observe((obstacle_motion_bbox_t){48,38,24,24},1200);
    T(s.tracks[0].confirmed&&r.count==1&&s.reference_valid[0]);
    tentative_start((obstacle_motion_bbox_t){48,38,24,24});
    tentative_observe((obstacle_motion_bbox_t){49,39,22,22},1100);
    T(!s.tracks[0].confirmed&&r.count==0);
    tentative_observe((obstacle_motion_bbox_t){50,40,20,20},1200);
    T(s.tracks[0].confirmed&&r.count==1);
    tentative_start((obstacle_motion_bbox_t){50,40,20,20});
    for(k=0;k<24u;++k){
        obstacle_motion_bbox_t b=(k&1u)?(obstacle_motion_bbox_t){50,40,20,20}:(obstacle_motion_bbox_t){49,39,22,22};
        tentative_observe(b,1100u+100u*k);
        T(!s.tracks[0].confirmed&&r.count==0);
    }
    tentative_start((obstacle_motion_bbox_t){50,40,20,20});
    tentative_observe((obstacle_motion_bbox_t){52,40,21,20},1100);
    tentative_observe((obstacle_motion_bbox_t){54,40,20,20},1200);
    T(s.tracks[0].confirmed&&r.count==1);
}
int main(void){confirmation_cases();reference_floor();shape_and_prediction();reference_confirmed_only();park_miss_and_recover();finite_release(50);finite_release(200);printf("targeted tracking: %u checks passed\n",checks);return 0;}
