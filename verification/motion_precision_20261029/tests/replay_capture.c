/* Offline retained-frame replay. An old-state import preserves the background,
 * previous frame and old associations; only the appended template/history is
 * initialized from that previous frame. This is not a new on-board capture. */
#include "obstacle_motion_core.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static obstacle_motion_t state;
static uint16_t pixels[320u*240u];
static int load_initial(FILE *fp) {
    long size;
    rewind(fp); if(fseek(fp,0,SEEK_END))return 0;size=ftell(fp);rewind(fp);
#ifdef OBSTACLE_TRACKING_V2
    if(size==430256L){
        unsigned t,c,x,y;
        if(fread(&state,1,offsetof(obstacle_motion_t,stable),fp)!=offsetof(obstacle_motion_t,stable))return 0;
        for(t=0;t<OBSTACLE_MOTION_MAX_OBJECTS;++t){
            obstacle_motion_bbox_t b=state.tracks[t].bbox;
            if(!state.tracks[t].used || !b.w || !b.h)continue;
            for(y=0;y<6u;++y)for(x=0;x<6u;++x){
                unsigned px=b.x+(2u*x+1u)*b.w/12u,py=b.y+(2u*y+1u)*b.h/12u;
                unsigned i=py*160u+px,k=y*6u+x;
                if(i>=OBSTACLE_MOTION_PIXELS)return 0;
                state.appearance[t][0][k]=state.previous[i];
                for(c=0;c<2u;++c)state.appearance[t][c+1u][k]=state.previous_chroma[c][i];
            }
            state.appearance_valid[t]=1;
        }
        printf("IMPORTED_V1_PREFIX=%u APPENDED_HISTORY_SEEDED_FROM_PREVIOUS_FRAME\n",(unsigned)offsetof(obstacle_motion_t,stable));
        return 1;
    }
#endif
#ifdef OBSTACLE_TRACKING_V3
    if(size==450328L){
        memset(&state,0,sizeof state);
        if(fread(&state,1,450328u,fp)!=450328u)return 0;
        puts("IMPORTED_V2_PREFIX_NO_VELOCITY_HISTORY");
        return 1;
    }
#endif
    return size==(long)sizeof state && fread(&state,1,sizeof state,fp)==sizeof state;
}
int main(int argc,char **argv) {
    FILE *in,*meta,*out,*initial;unsigned frame=0;uint32_t m[16];
    if(argc!=5)return 2;
    in=fopen(argv[1],"rb");meta=fopen(argv[2],"rb");out=fopen(argv[3],"wb");initial=strcmp(argv[4],"-")?fopen(argv[4],"rb"):NULL;
    if(!in||!meta||!out||(strcmp(argv[4],"-")&&!initial))return 3;
    if(initial){if(!load_initial(initial))return 4;fclose(initial);}
    else {obstacle_motion_init(&state);puts("REPLAY_FROM_RESET_NO_IMPORTED_HISTORY");}
    fputs("[\n",out);
    while(fread(pixels,1,sizeof pixels,in)==sizeof pixels && fread(m,1,sizeof m,meta)==sizeof m){
        obstacle_motion_result_t r;unsigned i;
        obstacle_motion_process(&state,pixels,320,240,m[0],m[2],m[3],&r);
        fprintf(out,"%s{\"frame\":%u,\"status\":%u,\"count\":%u,\"components\":%u,\"changed\":%u,\"fg\":%u,\"objects\":[",frame?",\n":"",frame,r.status,r.count,r.components,r.changed_pixels,r.foreground_pixels);
        for(i=0;i<r.count;++i){obstacle_motion_object_t *q=&r.objects[i];fprintf(out,"%s{\"id\":%u,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u,\"confidence\":%u}",i?",":"",q->id,q->bbox.x,q->bbox.y,q->bbox.w,q->bbox.h,q->confidence);}
        fputs("],\"candidates\":[",out);
        for(i=0;i<state.candidate_count;++i){obstacle_motion_component_t *q=&state.candidates[i];fprintf(out,"%s{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u,\"area\":%u,\"motion\":%u}",i?",":"",q->bbox.x*2u,q->bbox.y*2u,q->bbox.w*2u,q->bbox.h*2u,q->area,q->motion_pixels);}
        fputs("],\"tracks\":[",out);
        for(i=0;i<8u;++i){obstacle_motion_track_t *q=&state.tracks[i];fprintf(out,"%s{\"id\":%u,\"used\":%u,\"hits\":%u,\"confirmed\":%u,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u}",i?",":"",q->id,q->used,q->hits,q->confirmed,q->bbox.x*2u,q->bbox.y*2u,q->bbox.w*2u,q->bbox.h*2u);}
        fputs("]}",out);++frame;
    }
    fputs("\n]\n",out);fclose(in);fclose(meta);fclose(out);
    printf("REPLAY_CAPTURE_FRAMES=%u STATE=%u\n",frame,(unsigned)sizeof state);return frame>0u?0:5;
}
