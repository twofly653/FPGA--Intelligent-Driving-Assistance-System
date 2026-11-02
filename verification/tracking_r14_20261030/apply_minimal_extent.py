from pathlib import Path
H=Path(__file__).parent;P=H.parents[1];s=(H/'r13_reconstructed.c').read_text()
s=s.replace('#define TRACKING_V3_COOKIE 0x4f545633u','#define TRACKING_V3_COOKIE 0x4f545633u\n#define TRACKING_V4_COOKIE 0x4f545634u')
s=s.replace('    memset(s->velocity_valid,0,sizeof s->velocity_valid);','''    memset(s->velocity_valid,0,sizeof s->velocity_valid);
    s->tracking_v4_cookie=TRACKING_V4_COOKIE;
    memset(s->reference_valid,0,sizeof s->reference_valid);
    memset(s->last_supported_ms,0,sizeof s->last_supported_ms);
    s->stable_remainder_ms=0;''',1)
s=s.replace('if(s->tracking_v3_cookie!=TRACKING_V3_COOKIE)','if(s->tracking_v3_cookie!=TRACKING_V3_COOKIE || s->tracking_v4_cookie!=TRACKING_V4_COOKIE)')
s=s.replace('''    obstacle_motion_track_t *t=&s->tracks[slot];
    uint8_t patch[3][OBSTACLE_APPEARANCE_SAMPLES];
    unsigned best=0xffffffffu,scale;int dx,dy;''','''    obstacle_motion_track_t *t=&s->tracks[slot];
    const obstacle_motion_bbox_t *ref=s->reference_valid[slot]?&s->reference_bbox[slot]:&t->bbox;
    uint8_t patch[3][OBSTACLE_APPEARANCE_SAMPLES];
    unsigned best=0xffffffffu,scale;int dx,dy;''')
s=s.replace('t->bbox.w*(70u+15u*scale)','ref->w*(80u+10u*scale)').replace('t->bbox.h*(70u+15u*scale)','ref->h*(80u+10u*scale)')
s=s.replace('score=score*8u+(unsigned)(iabs(xx-cx)+iabs(yy-cy));','''score=score*8u+(unsigned)(iabs(xx-cx)+iabs(yy-cy));
            score+=(unsigned)(iabs((int)w-ref->w)+iabs((int)h-ref->h))*128u/umax(1u,ref->w+ref->h);''')
s=s.replace('''            s->appearance_valid[slot]=0;
        }
        else {''','''            s->appearance_valid[slot]=0;s->reference_valid[slot]=0;
        }
        else {''')
s=s.replace('        t->bbox=c->bbox; t->last_seen_ms=now; t->used=1;','''        /* Component evidence can update extent; continuation cannot recursively
         * reduce its own search scale on every frame. */
        if(t->confirmed){s->reference_bbox[slot]=c->bbox;s->reference_valid[slot]=1;}
        t->bbox=c->bbox; t->last_seen_ms=now; t->used=1;''')
s=s.replace('''        if(!current_motion(s,i,delta,threshold,chroma_threshold)){
            if(s->stable[i]<255u)++s->stable[i];
        }else s->stable[i]=0;''','''        if(s->gray[i] || current_motion(s,i,delta,threshold,chroma_threshold))s->stable[i]=0;
        else if(s->stable[i]<255u)++s->stable[i];''')
(P/'firmware/src/obstacle_motion_core.c').write_text(s)
