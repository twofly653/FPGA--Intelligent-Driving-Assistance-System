from pathlib import Path
import re,hashlib
H=Path(__file__).parent;s=(H/'starting_candidate_core.c').read_text()
s=s.replace('#define TRACKING_V4_COOKIE 0x4f545634u\n','')
a=s.index('    s->tracking_v4_cookie=TRACKING_V4_COOKIE;');b=s.index('\n}',a);s=s[:a]+s[b:]
s=s.replace('if(s->tracking_v3_cookie!=TRACKING_V3_COOKIE || s->tracking_v4_cookie!=TRACKING_V4_COOKIE)','if(s->tracking_v3_cookie!=TRACKING_V3_COOKIE)')
a=s.index('    if(t->confirmed){',s.index('static unsigned pair_cost'));b=s.index('    if(!t->confirmed && !has_motion',a);s=s[:a]+s[b:]
s=s.replace('(t->confirmed?2u:4u)*umin(a,b)','4u*umin(a,b)')
a=s.index('        int px=(int)t->bbox.x+dx');b=s.index('        unsigned appearance=',a)
s=s[:a]+'''        unsigned xa=umax(t->bbox.x,c->bbox.x),ya=umax(t->bbox.y,c->bbox.y);
        unsigned xb=umin(t->bbox.x+t->bbox.w,c->bbox.x+c->bbox.w);
        unsigned yb=umin(t->bbox.y+t->bbox.h,c->bbox.y+c->bbox.h);
        unsigned inter=xb>xa && yb>ya?(xb-xa)*(yb-ya):0u;
'''+s[b:]
s=s.replace('    const obstacle_motion_bbox_t *ref=s->reference_valid[slot]?&s->reference_bbox[slot]:&t->bbox;\n','')
s=s.replace('ref->w*(80u+10u*scale)','t->bbox.w*(70u+15u*scale)').replace('ref->h*(80u+10u*scale)','t->bbox.h*(70u+15u*scale)')
s=s.replace('            score+=(unsigned)(iabs((int)w-ref->w)+iabs((int)h-ref->h))*128u/umax(1u,ref->w+ref->h);\n','')
s=s.replace('s->appearance_valid[slot]=0;s->reference_valid[slot]=0;\n            s->last_supported_ms[slot]=now;','s->appearance_valid[slot]=0;')
a=s.index('        /* Learn extent only');b=s.index('        t->bbox=c->bbox;',a);s=s[:a]+s[b:]
s=s.replace(';occupied[j]=1;s->last_supported_ms[j]=now;',';occupied[j]=1;')
a=s.index('/* Separate rigid motion');b=s.index('static void assign_and_output',a);s=s[:a]+s[b:]
s=s.replace('''            if(confirmation_motion(t,c) &&
               c->motion_pixels>=umax(8u,c->area/24u))t->confirmed=1;''','''            int travel=iabs(2*(int)c->bbox.x+c->bbox.w-2*(int)t->anchor.x-t->anchor.w)+
                       iabs(2*(int)c->bbox.y+c->bbox.h-2*(int)t->anchor.y-t->anchor.h);
            int scale=iabs((int)c->bbox.w-t->anchor.w)+iabs((int)c->bbox.h-t->anchor.h);
            if((travel>=6 || (scale>=4 && c->motion_pixels*5u>=c->area)) &&
               c->motion_pixels>=umax(8u,c->area/24u))t->confirmed=1;''')
a=s.index('    {\n        unsigned elapsed=capture_ms');b=s.index('    /* Apply global affine',a)
s=s[:a]+'''    for(i=0;i<MN;++i){
        if(!current_motion(s,i,delta,threshold,chroma_threshold)){
            if(s->stable[i]<255u)++s->stable[i];
        }else s->stable[i]=0;
        if(!s->gray[i] && s->stable[i]>=12u){
            s->foreground[i]=0;
        }
    }
'''+s[b:]
(H/'r13_reconstructed.c').write_text(s)
print(hashlib.sha256(s.encode()).hexdigest())
