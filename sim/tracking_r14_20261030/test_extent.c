#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "obstacle_motion_core.c"
static obstacle_motion_t state;
static unsigned checks;
#define CHECK(c) do { ++checks; if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} } while(0)
int main(void)
{
    unsigned x,y,k;int photo[6]={0,0,0,256,256,256};
    obstacle_motion_component_t observed,c;
    obstacle_motion_track_t *t;
    obstacle_motion_init(&state);t=&state.tracks[0];
    t->used=t->confirmed=1;t->id=7;t->bbox=(obstacle_motion_bbox_t){50,40,40,40};t->last_seen_ms=1000;
    memset(state.smooth,64,sizeof state.smooth);memset(state.chroma,128,sizeof state.chroma);
    for(y=40;y<80;++y)for(x=50;x<90;++x){state.smooth[y*MW+x]=210;state.foreground[y*MW+x]=1;}
    sample_appearance(&state,t->bbox,state.appearance[0]);state.appearance_valid[0]=1;
    for(y=40;y<80;++y)for(x=50;x<90;++x)if(x<56||x>=84||y<46||y>=74)state.smooth[y*MW+x]=150;
    for(k=0;k<8;++k){
        CHECK(continue_track(&state,0,&observed,photo,12,10,1100+k*100));
        CHECK(observed.bbox.w*observed.bbox.h>=800u);
        t->bbox=observed.bbox;t->last_seen_ms=1100+k*100;
    }
    puts("PASS supported object extent does not collapse across repeated continuation");
    memset(state.foreground,0,sizeof state.foreground);
    CHECK(!continue_track(&state,0,&observed,photo,12,10,2000));
    puts("PASS prediction and old appearance alone cannot produce a box");
    obstacle_motion_init(&state);t=&state.tracks[0];
    t->used=t->confirmed=1;t->id=7;t->bbox=(obstacle_motion_bbox_t){50,40,20,20};t->last_seen_ms=1000;
    t->chroma_mean[0]=t->chroma_mean[1]=128;state.velocity_valid[0]=1;state.velocity_pps[0][0]=100;
    memset(&c,0,sizeof c);c.bbox=(obstacle_motion_bbox_t){60,40,20,20};c.area=400;c.motion_pixels=200;c.chroma_mean[0]=c.chroma_mean[1]=128;
    CHECK(pair_cost(&state,0,&c,1100)==0u);
    c.bbox.x=100;CHECK(pair_cost(&state,0,&c,1100)>0u);
    puts("PASS predicted location has zero distance and overlap penalty at capture time");
    printf("TRACKING_R14_PASS %u assertions\n",checks);return 0;
}
