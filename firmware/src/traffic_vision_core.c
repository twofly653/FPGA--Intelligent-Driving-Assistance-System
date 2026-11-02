#include "traffic_vision_core.h"
#include <string.h>

/* Independent, integer-only traffic light / seven-segment demonstration core.
 * This is constrained appearance recognition, not a safety-rated detector. */
typedef struct { unsigned x0,y0,x1,y1; } box_t;
static unsigned imin(unsigned a,unsigned b) { return a<b?a:b; }
static unsigned imax(unsigned a,unsigned b) { return a>b?a:b; }
static unsigned diff(unsigned a,unsigned b) { return a>b?a-b:b-a; }
static void rgb(uint16_t p,unsigned *r,unsigned *g,unsigned *b) {
    unsigned rr=(p>>11)&31u,gg=(p>>5)&63u,bb=p&31u;
    *r=(rr<<3)|(rr>>2); *g=(gg<<2)|(gg>>4); *b=(bb<<3)|(bb>>2);
}
static unsigned classify(uint16_t p) {
    unsigned r,g,b,hi,lo,chroma;
    /* Every accepted hue needs R or G >=104. Reject a dark pixel in its
     * native 5/6-bit channels before expansion and HSV arithmetic. */
    if(((p>>11)&31u)<13u && ((p>>5)&63u)<26u) return TRAFFIC_UNKNOWN;
    rgb(p,&r,&g,&b);
    hi=imax(r,imax(g,b));lo=imin(r,imin(g,b));chroma=hi-lo;
    /* Integer HSV-style gates: hue uses channel differences, so a modest
     * neutral veil does not rotate it; relative saturation rejects grey/glare.
     * Keep a real brightness/chroma floor: very dim/fog-washed pixels are
     * unknown, never amplified or assigned a colour by their largest channel. */
    if(hi<104u || chroma<52u || chroma*100u<hi*35u) return TRAFFIC_UNKNOWN;
    /* Amber precedes red. Compare hue after removing the blue baseline. */
    if(r>=b+50u && g>=b+35u && (g-b)*100u>=(r-b)*42u &&
       (g-b)*100u<=(r-b)*120u) return TRAFFIC_AMBER;
    if(r>=g+40u && r>=b+50u && b<=g+25u &&
       (g-lo)*100u<=chroma*35u) return TRAFFIC_RED;
    if(g>=r+35u && g>=b+30u && (r-lo)*100u<=chroma*55u &&
       (b-lo)*100u<=chroma*55u) return TRAFFIC_GREEN;
    return TRAFFIC_UNKNOWN;
}
static unsigned lamp_family(unsigned c) { return c==TRAFFIC_AMBER?TRAFFIC_RED:c; }
static int lamp_pixel(uint16_t p,unsigned selector) {
    unsigned c=classify(p);
    return selector==TRAFFIC_UNKNOWN?(c==TRAFFIC_RED || c==TRAFFIC_AMBER):c==selector;
}
static unsigned lamp_color(const uint16_t *f,unsigned w,box_t b,unsigned family,unsigned area) {
    unsigned x,y,red=0,amber=0,red_all=0,amber_all=0,stable=0,clear_clipped_amber=0,outer_red[4]={0,0,0,0};
    if(family==TRAFFIC_GREEN) return TRAFFIC_GREEN;
    /* R-channel clipping can move a bright red centre into the amber hue band.
     * Use the connected physical shape, but determine warm hue from its
     * unclipped pixels. A clipped centre alone is insufficient colour evidence.
     * This gate also rejects genuine red/amber mixtures instead of voting a
     * bare majority or interpreting one colour as a second physical lamp. */
    for(y=b.y0;y<=b.y1;++y)for(x=b.x0;x<=b.x1;++x) {
        uint16_t p=f[y*w+x];unsigned c=classify(p);
        if(c==TRAFFIC_RED)++red_all;else if(c==TRAFFIC_AMBER)++amber_all;
        if((p>>11)==31u || ((p>>5)&63u)==63u) {
            unsigned r,g,bl;rgb(p,&r,&g,&bl);
            /* A saturated R channel cannot determine the original hue.
             * The clipped red core may enter the 30..40deg warm band;
             * only a clearly yellow (>40deg, with one quantization-step
             * margin) core vetoes a coherent, unclipped red rim. */
            if(c==TRAFFIC_AMBER && (g-bl)*3u>(r-bl)*2u+16u)++clear_clipped_amber;
            continue;
        }
        if(c==TRAFFIC_RED) {
            unsigned dx=diff(2u*x,b.x0+b.x1),dy=diff(2u*y,b.y0+b.y1);
            unsigned bw=b.x1-b.x0,bh=b.y1-b.y0;
            ++red;
            if(9u*(dx*dx*bh*bh+dy*dy*bw*bw)>=4u*bw*bw*bh*bh)
                ++outer_red[(2u*x>b.x0+b.x1?1u:0u)+(2u*y>b.y0+b.y1?2u:0u)];
        } else if(c==TRAFFIC_AMBER)++amber;
    }
    /* Preserve a coherent single-hue lamp, including a uniformly bright
     * red/amber source; the clipping disambiguation applies to mixed hues. */
    if(!amber_all && red_all) return TRAFFIC_RED;
    if(!red_all && amber_all) return TRAFFIC_AMBER;
    stable=red+amber;
    if(stable<imax(8u,area/8u)) return TRAFFIC_UNKNOWN;
    if(red*100u>=stable*85u && !clear_clipped_amber) {
        unsigned k,needed=imax(1u,red/16u);
        /* A red rim around a clipped centre needs support on every side.
         * A red centre, one red side, or a second touching lamp is not enough. */
        for(k=0;k<4u;++k)if(outer_red[k]<needed)return TRAFFIC_UNKNOWN;
        return TRAFFIC_RED;
    }
    if(amber*100u>=stable*85u) return TRAFFIC_AMBER;
    return TRAFFIC_UNKNOWN;
}
static unsigned bright(uint16_t p) { unsigned r,g,b;rgb(p,&r,&g,&b);return imax(r,imax(g,b)); }
static int ink(uint16_t p,unsigned color,unsigned continuous_warm) {
    unsigned r,g,b,hi,lo;
    if(classify(p)==color || (continuous_warm && lamp_family(classify(p))==lamp_family(color))) return 1;
    rgb(p,&r,&g,&b);hi=imax(r,imax(g,b));lo=imin(r,imin(g,b));
    if(continuous_warm && (color==TRAFFIC_RED||color==TRAFFIC_AMBER) && r>=104u && r>=g+40u && g>=b && r>=b+52u &&
       (g-b)*100u>=(r-b)*35u && (g-b)*100u<=(r-b)*42u)return 1;
    return hi>=180u && lo>=150u && hi-lo<=60u;
}
static traffic_vision_bbox_t bbox(box_t b) {
    traffic_vision_bbox_t t; t.x=(uint16_t)b.x0;t.y=(uint16_t)b.y0;
    t.w=(uint16_t)(b.x1-b.x0+1u);t.h=(uint16_t)(b.y1-b.y0+1u);return t;
}
static void empty_result(traffic_vision_result_t *r) { memset(r,0,sizeof(*r));r->countdown=-1; }
void traffic_vision_reset(traffic_vision_t *s) {
    if(s!=0) { s->candidate_color=0;s->color_streak=0;s->candidate_digit=-1;s->digit_streak=0;s->digit_misses=0;memset(&s->candidate_lamp,0,sizeof s->candidate_lamp); }
}
void traffic_vision_init(traffic_vision_t *s) {
    if(s==0) return;
    memset(s,0,sizeof(*s));s->color_confirm_frames=2;s->digit_confirm_frames=2;
    s->min_confidence=72;s->min_lamp_diameter=6;s->max_lamp_diameter=40;
    s->candidate_digit=-1;
}
void traffic_vision_set_roi(traffic_vision_t *s,unsigned x,unsigned y,unsigned w,unsigned h) {
    if(s==0) return;
    s->roi.x=(uint16_t)imin(x,TRAFFIC_VISION_MAX_WIDTH);
    s->roi.y=(uint16_t)imin(y,TRAFFIC_VISION_MAX_HEIGHT);
    s->roi.w=(uint16_t)imin(w,TRAFFIC_VISION_MAX_WIDTH);
    s->roi.h=(uint16_t)imin(h,TRAFFIC_VISION_MAX_HEIGHT);traffic_vision_reset(s);
}
static unsigned dark_ring(const uint16_t *f,unsigned w,unsigned h,box_t b,unsigned margin) {
    unsigned x,y,n=0,dark=0;
    unsigned x0=b.x0>margin?b.x0-margin:0,y0=b.y0>margin?b.y0-margin:0;
    unsigned x1=imin(w-1u,b.x1+margin),y1=imin(h-1u,b.y1+margin);
    if(b.x0<margin || b.y0<margin || b.x1+margin>=w || b.y1+margin>=h) return 0;
    for(y=y0;y<=y1;++y) for(x=x0;x<=x1;++x) {
        if(x>=b.x0 && x<=b.x1 && y>=b.y0 && y<=b.y1) continue;
        ++n;if(bright(f[y*w+x])<=80u) ++dark;
    }
    return n?100u*dark/n:0;
}
static int neutral_glare(uint16_t p) {
    unsigned r,g,b,hi,lo;rgb(p,&r,&g,&b);
    hi=imax(r,imax(g,b));lo=imin(r,imin(g,b));
    return lo>=150u && hi-lo<=60u;
}
static int shape_pixel(uint16_t p,unsigned color,unsigned glare) {
    return lamp_pixel(p,color)||(glare&&neutral_glare(p));
}
static unsigned colour_span(const uint16_t *f,unsigned w,box_t b,unsigned y,unsigned color,unsigned glare) {
    unsigned x,lo=b.x1+1u,hi=b.x0;
    for(x=b.x0;x<=b.x1;++x)if(shape_pixel(f[y*w+x],color,glare)){lo=imin(lo,x);hi=imax(hi,x);}
    return lo<=hi?hi-lo+1u:0u;
}
static int lamp_shape_inner(const uint16_t *f,unsigned w,unsigned h,box_t b,unsigned area,
                      unsigned sx,unsigned sy,unsigned color,unsigned glare,unsigned *score) {
    unsigned bw=b.x1-b.x0+1u,bh=b.y1-b.y0+1u,fill,ring,corners=0;
    unsigned cx=sx/area,cy=sy/area,middle,upper,lower;
    if(bw*4u<bh*3u || bh*4u<bw*3u) return 0;
    fill=100u*area/(bw*bh);
    if(fill<48u || fill>87u || diff(cx,(b.x0+b.x1)/2u)>bw/5u || diff(cy,(b.y0+b.y1)/2u)>bh/5u) return 0;
    corners+=(shape_pixel(f[b.y0*w+b.x0],color,glare));
    corners+=(shape_pixel(f[b.y0*w+b.x1],color,glare));
    corners+=(shape_pixel(f[b.y1*w+b.x0],color,glare));
    corners+=(shape_pixel(f[b.y1*w+b.x1],color,glare));
    if(corners>1u) return 0;
    /* A filled emitter has a lit core. Tilted seven-segment 0/6/8/9 can
     * fragment into round-looking C shapes that pass bbox/fill tests but
     * leave a dark centre. Keep small neutral glare as a lit core sample. */
    {unsigned ix,iy,core=0;
     for(iy=4u;iy<=6u;++iy)for(ix=4u;ix<=6u;++ix){
         uint16_t p=f[(b.y0+iy*(bh-1u)/10u)*w+b.x0+ix*(bw-1u)/10u];
         if(lamp_pixel(p,color))++core;
         else {unsigned r,g,bl;rgb(p,&r,&g,&bl);
             if(imin(r,imin(g,bl))>=150u&&imax(r,imax(g,bl))-imin(r,imin(g,bl))<=60u)++core;}
     }
     if(core<7u)return 0;
    }
    middle=colour_span(f,w,b,(b.y0+b.y1)/2u,color,glare);
    upper=colour_span(f,w,b,b.y0+bh/4u,color,glare);
    lower=colour_span(f,w,b,b.y1-bh/4u,color,glare);
    if(middle==0u||upper*100u<middle*65u||lower*100u<middle*65u)return 0;
    ring=dark_ring(f,w,h,b,imax(2u,imin(bw,bh)/3u));
    if(ring<72u) { *score=0;return -1; }
    *score=(ring+100u-imin(100u,diff(fill,74u)*2u))/2u;
    return 1;
}
static int lamp_shape(const uint16_t *f,unsigned w,unsigned h,box_t b,unsigned area,
                      unsigned sx,unsigned sy,unsigned color,unsigned *score) {
    return lamp_shape_inner(f,w,h,b,area,sx,sy,color,0u,score);
}
/* A clipped display emitter can retain colour only in a broken thin rim.
 * Recover its connected neutral core in a strictly local window, never by
 * classifying white pixels as traffic colours over the whole frame. Recovery
 * retains substantial chromatic support in all four quadrants, then reuses
 * the original circular shape, bright-core and dark-housing gates. The high
 * mask bit is temporary visited storage and is always removed on return. */
static int lamp_recover_glare(traffic_vision_t *s,const uint16_t *f,unsigned w,unsigned h,
                             unsigned seed,unsigned family,box_t *b,unsigned *area,
                             unsigned *sx,unsigned *sy,unsigned *score) {
    unsigned bw=b->x1-b->x0+1u,bh=b->y1-b->y0+1u;
    unsigned margin=(bw<s->min_lamp_diameter||bh<s->min_lamp_diameter)?s->max_lamp_diameter:imin(8u,imax(4u,imin(bw,bh)/2u));
    unsigned x0=b->x0>margin?b->x0-margin:0u,y0=b->y0>margin?b->y0-margin:0u;
    unsigned x1=imin(w-1u,b->x1+margin),y1=imin(h-1u,b->y1+margin);
    unsigned head=0,tail=0,colour=0,glare=0,sumx=0,sumy=0,quad[4]={0,0,0,0};
    unsigned x,y,k,ok=0,near_glare=0,cutoff=0;box_t recovered={w,h,0,0};
    if(*area<4u||bw>s->max_lamp_diameter||bh>s->max_lamp_diameter)return 0;
    /* Tiny perimeter fragments can seed recovery, but the recovered emitter
     * must still satisfy the unchanged physical diameter and shape gates.
     * Require a real nearby neutral core before any bounded flood fill. */
    for(y=b->y0?b->y0-1u:0u;y<=imin(h-1u,b->y1+1u)&&!near_glare;++y)
        for(x=b->x0?b->x0-1u:0u;x<=imin(w-1u,b->x1+1u);++x)
            if(neutral_glare(f[y*w+x])){near_glare=1u;break;}
    if(!near_glare)return 0;
    s->queue[tail++]=((seed/w)<<16)|(seed%w);s->mask[seed]|=128u;
    while(head<tail){
        unsigned q=s->queue[head++],xx=q&65535u,yy=q>>16,p=yy*w+xx,c=classify(f[p]);
        if(c&&lamp_family(c)==family)++colour;else ++glare;
        sumx+=xx;sumy+=yy;
        recovered.x0=imin(recovered.x0,xx);recovered.x1=imax(recovered.x1,xx);
        recovered.y0=imin(recovered.y0,yy);recovered.y1=imax(recovered.y1,yy);
        if(recovered.x1-recovered.x0+1u>s->max_lamp_diameter||
           recovered.y1-recovered.y0+1u>s->max_lamp_diameter){cutoff=1u;break;}
        for(k=0;k<4u;++k){
            unsigned nx=xx,ny=yy,np,nc;
            if(k==0u){if(xx<=x0)continue;--nx;}
            else if(k==1u){if(xx>=x1)continue;++nx;}
            else if(k==2u){if(yy<=y0)continue;--ny;}
            else {if(yy>=y1)continue;++ny;}
            np=ny*w+nx;if(s->mask[np]&128u)continue;nc=classify(f[np]);
            if((nc&&lamp_family(nc)==family)||neutral_glare(f[np])){
                s->mask[np]|=128u;s->queue[tail++]=(ny<<16)|nx;
            }
        }
    }
    bw=recovered.x1-recovered.x0+1u;bh=recovered.y1-recovered.y0+1u;
    if(!cutoff && head==tail && glare && colour>=8u && colour*100u>=tail*15u &&
       recovered.x0>x0&&recovered.y0>y0&&recovered.x1<x1&&recovered.y1<y1&&
       bw>=s->min_lamp_diameter&&bh>=s->min_lamp_diameter&&
       bw<=s->max_lamp_diameter&&bh<=s->max_lamp_diameter){
        for(k=0;k<tail;++k){unsigned q=s->queue[k],xx=q&65535u,yy=q>>16,c=classify(f[yy*w+xx]);
            if(c&&lamp_family(c)==family)
                ++quad[(2u*xx>recovered.x0+recovered.x1?1u:0u)+(2u*yy>recovered.y0+recovered.y1?2u:0u)];
        }
        ok=1u;for(k=0;k<4u;++k)if(quad[k]<imax(1u,colour/32u))ok=0;
        if(ok)ok=lamp_shape_inner(f,w,h,recovered,tail,sumx,sumy,
                                 family==TRAFFIC_RED?TRAFFIC_UNKNOWN:family,1u,score)>0&&*score>=s->min_confidence;
    }
    for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x){
        unsigned p=y*w+x;
        if(ok&&(s->mask[p]&128u))s->mask[p]=0;
        else s->mask[p]&=127u;
    }
    if(ok){*b=recovered;*area=tail;*sx=sumx;*sy=sumy;}
    return (int)ok;
}
/* A warm union must not hide two individually credible physical lamps.
 * Reuse the existing mask/queue inside this already-small candidate region;
 * no new frame buffer or large automatic arrays are allocated. */
static unsigned warm_monochrome_lamps(traffic_vision_t *s,const uint16_t *f,unsigned w,unsigned h,box_t whole,unsigned area) {
    unsigned i,x,y,lamps=0;
    for(i=0;i<area;++i) {unsigned q=s->queue[i];s->mask[(q>>16)*w+(q&65535u)]=(uint8_t)(4u|classify(f[(q>>16)*w+(q&65535u)]));}
    for(y=whole.y0;y<=whole.y1;++y)for(x=whole.x0;x<=whole.x1;++x) {
        unsigned idx=y*w+x,c=s->mask[idx],head=0,tail=0,n=0,sx=0,sy=0,score=0;box_t b;
        if(c!=(4u|TRAFFIC_RED) && c!=(4u|TRAFFIC_AMBER))continue;
        b.x0=b.x1=x;b.y0=b.y1=y;s->mask[idx]=0;s->queue[tail++]=(y<<16)|x;
        while(head<tail) {
            unsigned q=s->queue[head++],xx=q&65535u,yy=q>>16,p=yy*w+xx;
            ++n;sx+=xx;sy+=yy;b.x0=imin(b.x0,xx);b.x1=imax(b.x1,xx);b.y0=imin(b.y0,yy);b.y1=imax(b.y1,yy);
            if(xx>0 && s->mask[p-1u]==c){s->mask[p-1u]=0;s->queue[tail++]=q-1u;}
            if(xx+1u<w && s->mask[p+1u]==c){s->mask[p+1u]=0;s->queue[tail++]=q+1u;}
            if(yy>0 && s->mask[p-w]==c){s->mask[p-w]=0;s->queue[tail++]=q-65536u;}
            if(yy+1u<h && s->mask[p+w]==c){s->mask[p+w]=0;s->queue[tail++]=q+65536u;}
        }
        if(b.x1-b.x0+1u<s->min_lamp_diameter||b.y1-b.y0+1u<s->min_lamp_diameter||b.x1-b.x0+1u>s->max_lamp_diameter||b.y1-b.y0+1u>s->max_lamp_diameter)continue;
        if(lamp_shape(f,w,h,b,n,sx,sy,c&3u,&score)>0 && score>=s->min_confidence)++lamps;
        if(lamps>1u)return lamps;
    }
    return lamps;
}
static int in_box(unsigned x,unsigned y,box_t b) {return x>=b.x0&&x<=b.x1&&y>=b.y0&&y<=b.y1;}
static int digit_ink(const traffic_vision_t *s,unsigned w,unsigned x,unsigned y,box_t lamp) {
    if(in_box(x,y,lamp)) return 0;
    return s->mask[y*w+x]!=0;
}
static const unsigned char seven[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
static int canonical_ink(unsigned d,unsigned x,unsigned y,unsigned thick,unsigned optical_gap) {
    unsigned m=seven[d];
    if(thick!=3u||optical_gap){unsigned gap=optical_gap?optical_gap:thick;unsigned mid0=14u-gap/2u,mid1=mid0+gap-1u;
        return ((m&1u)&&x>=1u&&x<=13u&&y<thick)||((m&2u)&&x>=15u-thick&&y>=thick&&y<mid0)||
          ((m&4u)&&x>=15u-thick&&y>mid1&&y<29u-thick)||((m&8u)&&x>=1u&&x<=13u&&y>=29u-thick)||
          ((m&16u)&&x<thick&&y>mid1&&y<29u-thick)||((m&32u)&&x<thick&&y>=thick&&y<mid0)||
          ((m&64u)&&x>=1u&&x<=13u&&y>=mid0&&y<=mid1);
    }
    if((m&1u)&&x>=1u&&x<=13u&&y<=2u) return 1;
    if((m&2u)&&x>=12u&&y>=3u&&y<=12u) return 1;
    if((m&4u)&&x>=12u&&y>=16u&&y<=25u) return 1;
    if((m&8u)&&x>=1u&&x<=13u&&y>=26u) return 1;
    if((m&16u)&&x<=2u&&y>=16u&&y<=25u) return 1;
    if((m&32u)&&x<=2u&&y>=3u&&y<=12u) return 1;
    if((m&64u)&&x>=1u&&x<=13u&&y>=13u&&y<=15u) return 1;
    return 0;
}
static void canonical_bounds(unsigned d,box_t *b,unsigned thick) {
    /* Exact occupied bounds of the immutable 15x29 templates. Avoid scanning
     * all 435 template pixels ten times for each observed glyph. */
    static const unsigned char bounds[10][4]={
        {0,0,14,28},{12,3,14,25},{0,0,14,28},{1,0,14,28},{0,3,14,25},
        {0,0,14,28},{0,0,14,28},{1,0,14,25},{0,0,14,28},{0,0,14,28}};
    b->x0=bounds[d][0];b->y0=bounds[d][1];b->x1=bounds[d][2];b->y1=bounds[d][3];
    if(thick!=3u){if(d==1u){b->x0=15u-thick;b->y0=thick;b->y1=28u-thick;}else if(d==4u){b->y0=thick;b->y1=28u-thick;}else if(d==7u)b->y1=28u-thick;}
}
static int segment_evidence(const traffic_vision_t *s,unsigned w,box_t b,box_t cb,unsigned d,box_t lamp,unsigned thick) {
    unsigned zones[7][4]={{3,0,11,2},{12,5,14,10},{12,18,14,23},{3,26,11,28},{0,18,2,23},{0,5,2,10},{3,13,11,15}};
    /* An absent middle bar is checked away from thick side strokes. Keep
     * original on-segment evidence zones, including narrow digit 1. */
    if(!(seven[d]&64u)){zones[6][0]=thick+1u;zones[6][2]=13u-thick;}
    unsigned k,bw=b.x1-b.x0,bh=b.y1-b.y0,cw=cb.x1-cb.x0,ch=cb.y1-cb.y0;
    for(k=0;k<7u;++k) {
        unsigned a,z,hit=0,total=0,centre_hit=0;
        if(zones[k][0]<cb.x0||zones[k][2]>cb.x1||zones[k][1]<cb.y0||zones[k][3]>cb.y1)continue;
        for(z=0;z<3u;++z)for(a=0;a<3u;++a) {
            unsigned cx=zones[k][0]+a*(zones[k][2]-zones[k][0])/2u;
            unsigned cy=zones[k][1]+z*(zones[k][3]-zones[k][1])/2u;
            unsigned x=b.x0+((cx-cb.x0)*bw+cw/2u)/cw,y=b.y0+((cy-cb.y0)*bh+ch/2u)/ch;
            ++total;hit+=(unsigned)digit_ink(s,w,x,y,lamp);
            if(a==1u)centre_hit+=(unsigned)digit_ink(s,w,x,y,lamp);
        }
        if((seven[d]&(1u<<k))?(hit*100u<total*45u):(hit*100u>total*30u))return 0;
        /* Two thick vertical legs are not a horizontal crossbar. Require
         * actual centre-column evidence for A/D/G, even if side votes pass. */
        if((k==0u||k==3u||k==6u)&&(seven[d]&(1u<<k))&&!centre_hit)return 0;
    }
    if(d==1u) {
        unsigned x,z,upper=0,lower=0,thin=0,empty_middle=0,reference;
        /* Optical blur can bridge the B/C gap at one centre pixel. Require
         * two genuinely narrowed middle rows relative to BOTH bright lobes,
         * rather than an exactly empty centre column. A uniform luminous
         * pole has no waist and still cannot pass as digit 1. */
        for(x=b.x0;x<=b.x1;++x) {
            upper+=(unsigned)digit_ink(s,w,x,b.y0+bh/4u,lamp);
            lower+=(unsigned)digit_ink(s,w,x,b.y0+3u*bh/4u,lamp);
        }
        reference=imin(upper,lower);
        if(reference<2u)return 0;
        for(z=b.y0+bh*40u/100u;z<=b.y0+bh*60u/100u;++z) {
            unsigned count=0;
            for(x=b.x0;x<=b.x1;++x)count+=(unsigned)digit_ink(s,w,x,z,lamp);
            if(count*100u<=reference*60u)++thin;
            if(!count)++empty_middle;
        }
        /* A small optical digit can retain only ONE completely dark gap
         * row after sampling. Its two dense lobes still need the template,
         * aspect, coverage and dark-housing checks. A uniform pole has no
         * empty middle row and cannot use this bounded exception. */
        if(thin<2u && !(empty_middle&&reference>=3u))return 0;
    }
    return 1;
}
static int decode_digit_exact(const traffic_vision_t *s,const uint16_t *f,unsigned w,unsigned h,box_t b,box_t lamp,unsigned *conf,unsigned flexible) {
    unsigned bw=b.x1-b.x0+1u,bh=b.y1-b.y0+1u,d,x,y,thick,variant,gap_variant,best=0,second=0,scores[10]={0};int answer=-1;
    unsigned one_width=0,one_upper=0,one_lower=0;
    unsigned one_y0=b.y0+(bh-1u)/4u,one_y1=b.y0+3u*(bh-1u)/4u;
    /* Fit digit 1's two lobes to their own shared stroke width. A one-pixel
     * camera slant otherwise inflates the glyph bbox by 25-50 percent. Use
     * only two measured lobe centres; do not follow arbitrary row noise. */
    {unsigned l[2]={b.x1+1u,b.x1+1u},r[2]={b.x0,b.x0},n[2]={0,0},k;
     for(k=0;k<2u;++k)for(x=b.x0;x<=b.x1;++x)if(digit_ink(s,w,x,k?one_y1:one_y0,lamp)){
         l[k]=imin(l[k],x);r[k]=imax(r[k],x);++n[k];
     }
     if(n[0]>=2u&&n[1]>=2u&&n[0]*100u>=(r[0]-l[0]+1u)*76u&&n[1]*100u>=(r[1]-l[1]+1u)*76u){
         one_width=((r[0]-l[0]+1u)+(r[1]-l[1]+1u)+1u)/2u;
         one_upper=(l[0]+r[0])*128u;one_lower=(l[1]+r[1])*128u;
         if(diff(one_upper,one_lower)>one_width*128u)one_width=0;
     }}
    if(bh<12u||bh>56u||bw<2u||bw>42u||dark_ring(f,w,h,b,2u)<65u) return -1;
    for(gap_variant=0;gap_variant<2u;++gap_variant){
    for(thick=flexible?2u:3u;thick<=(flexible?5u:3u);++thick)for(d=0;d<10u;++d)for(variant=0;variant<(d==1u?2u:1u);++variant) {
        box_t cb;unsigned actual=0,expect=0,intersect=0,score,cw,ch;
        uint8_t map_x[42];
        if(gap_variant&&thick==2u)continue;
        canonical_bounds(d,&cb,thick);cw=cb.x1-cb.x0+1u;ch=cb.y1-cb.y0+1u;
        /* Reject incompatible aspect ratios before comparing normalized glyphs. */
        if(variant&&!one_width)continue;
        {unsigned aw=variant?one_width:bw;
         if(aw*ch*100u<cw*bh*65u || aw*ch*100u>cw*bh*145u) continue;}
        if(!segment_evidence(s,w,b,cb,d,lamp,thick))continue;
        for(x=0;x<bw;++x)map_x[x]=(uint8_t)(cb.x0+(x*(cw-1u)+(bw-1u)/2u)/(bw-1u));
        for(y=b.y0;y<=b.y1;++y) {
            unsigned yy=cb.y0+((y-b.y0)*(ch-1u)+(bh-1u)/2u)/(bh-1u);
            int left=0,bar_on=0;
            if(variant){
                unsigned sy=imin(imax(y,one_y0),one_y1);
                int centre=(int)one_upper+((int)one_lower-(int)one_upper)*(int)(sy-one_y0)/(int)(one_y1-one_y0);
                left=(centre-(int)(one_width-1u)*128+128)/256;
                bar_on=canonical_ink(d,cb.x0,yy,thick,gap_variant?2u:0u);
            }
            for(x=b.x0;x<=b.x1;++x) {
                int a=digit_ink(s,w,x,y,lamp);
                int e=variant?(bar_on&&(int)x>=left&&(int)x<left+(int)one_width):canonical_ink(d,map_x[x-b.x0],yy,thick,gap_variant?2u:0u);
                actual+=(unsigned)a;expect+=(unsigned)e;intersect+=(unsigned)(a&&e);
            }
        }
        if(actual==0u||expect==0u||intersect*100u<actual*76u||intersect*100u<expect*76u) continue;
        score=200u*intersect/(actual+expect);
        if(score>scores[d])scores[d]=score;
    }
    best=second=0;answer=-1;
    for(d=0;d<10u;++d)if(scores[d]>best){second=best;best=scores[d];answer=(int)d;}else if(scores[d]>second)second=scores[d];
    if(best>=84u&&best-second>=(best>=94u?5u:7u)){*conf=best;return answer;}
    } /* Only try the optical gap model after the original model rejects. */
    return -1;
}
/* A perspective resample can add one sparse border pixel, stretching the
 * normalization of the entire small glyph. Retry one bounded robust bbox;
 * never erase a significant stroke or search arbitrary crop combinations. */
static int decode_digit(const traffic_vision_t *s,const uint16_t *f,unsigned w,unsigned h,
 box_t b,box_t lamp,unsigned *conf,unsigned flexible){
 int result=decode_digit_exact(s,f,w,h,b,lamp,conf,flexible);
 unsigned x,y,total=0,remaining=0,row_peak=0,col_peak=0,left=0,right=0,top=0,bottom=0;box_t t=b;
 if(result>=0)return result;
 for(y=b.y0;y<=b.y1;++y){unsigned count=0;for(x=b.x0;x<=b.x1;++x)count+=(unsigned)digit_ink(s,w,x,y,lamp);row_peak=imax(row_peak,count);total+=count;if(y==b.y0)top=count;if(y==b.y1)bottom=count;}
 for(x=b.x0;x<=b.x1;++x){unsigned count=0;for(y=b.y0;y<=b.y1;++y)count+=(unsigned)digit_ink(s,w,x,y,lamp);col_peak=imax(col_peak,count);if(x==b.x0)left=count;if(x==b.x1)right=count;}
 if(b.x1>b.x0+2u){if(left*2u<col_peak)++t.x0;if(right*2u<col_peak)--t.x1;}
 if(b.y1>b.y0+12u){if(top*2u<row_peak)++t.y0;if(bottom*2u<row_peak)--t.y1;}
 if(t.x0==b.x0&&t.x1==b.x1&&t.y0==b.y0&&t.y1==b.y1)return -1;
 for(y=t.y0;y<=t.y1;++y)for(x=t.x0;x<=t.x1;++x)remaining+=(unsigned)digit_ink(s,w,x,y,lamp);
 if(!total||remaining*100u<total*90u)return -1;
 return decode_digit_exact(s,f,w,h,t,lamp,conf,flexible);
}
/* Only small bright strokes with darker surroundings seed OCR. A bounded
 * two-pixel connection below restores coloured corners, never whole walls. */
static int stroke_on_dark(const uint16_t *f,unsigned w,unsigned h,unsigned x,unsigned y) {
    unsigned k,limit=imax(80u,bright(f[y*w+x])/2u);
    for(k=2;k<=6u;++k) {
        if(x>=k && x+k<w && bright(f[y*w+x-k])<=limit && bright(f[y*w+x+k])<=limit)return 1;
        if(y>=k && y+k<h && bright(f[(y-k)*w+x])<=limit && bright(f[(y+k)*w+x])<=limit)return 1;
    }
    return 0;
}
static int find_digits_base(traffic_vision_t *s,const uint16_t *f,unsigned w,unsigned h,
                       box_t lamp,unsigned color,box_t *out,unsigned *confidence,
                       traffic_vision_diagnostics_t *diag,unsigned full_panel) {
    unsigned d=imax(lamp.x1-lamp.x0+1u,lamp.y1-lamp.y0+1u),x,y;
    unsigned x0=lamp.x0>4u*d?lamp.x0-4u*d:0u,y0=lamp.y0>2u*d?lamp.y0-2u*d:0u;
    unsigned x1=imin(w-1u,lamp.x1+6u*d),y1=imin(h-1u,lamp.y1+4u*d);
    unsigned gap=imax(3u,d/3u+1u),start=0,last=0;int active=0,found=-1;unsigned found_conf=0;
    box_t excluded=lamp;excluded.x0=lamp.x0>2?lamp.x0-2:0;excluded.y0=lamp.y0>2?lamp.y0-2:0;
    excluded.x1=imin(w-1,lamp.x1+2);excluded.y1=imin(h-1,lamp.y1+2);
    /* Rectified edge pixels can sample the bright wall outside a dark panel
     * through integer corner rounding. Exclude its one-pixel perimeter;
     * glyphs still require the existing independent dark-ring padding. */
    if(full_panel){x0=y0=1;x1=w-2u;y1=h-2u;excluded.x0=excluded.x1=w;excluded.y0=excluded.y1=h;}
    /* Suppress isolated bright pixels before projecting. Three-pixel LED
     * strokes survive; single hot pixels cannot merge every row into a band. */
    memset(s->mask,0,w*h);
    /* Local dark-sided stroke seeds reject bright screen backgrounds on
     * the upright path as well as rectified panels. Preserve coloured corners
     * by the same bounded two-pixel growth, never a global threshold drop. */
    for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)
        if(!in_box(x,y,excluded)&&ink(f[y*w+x],color,full_panel))s->mask[y*w+x]=stroke_on_dark(f,w,h,x,y)?2:1;
    {unsigned pass;for(pass=0;pass<2u;++pass){
        for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)if(s->mask[y*w+x]==1u){unsigned xx,yy,hit=0;
            for(yy=y?y-1u:0u;yy<=imin(h-1u,y+1u);++yy)for(xx=x?x-1u:0u;xx<=imin(w-1u,x+1u);++xx)hit+=(s->mask[yy*w+xx]==2u);
            if(hit)s->mask[y*w+x]=3;
        }
        for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)if(s->mask[y*w+x]==3u)s->mask[y*w+x]=2;
    }}
    for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)s->mask[y*w+x]=(uint8_t)(s->mask[y*w+x]==2u);
    for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)if(s->mask[y*w+x]) {
        unsigned xx,yy,count=0;
        for(yy=y?y-1u:0u;yy<=imin(h-1u,y+1u);++yy)
            for(xx=x?x-1u:0u;xx<=imin(w-1u,x+1u);++xx)count+=(s->mask[yy*w+xx]!=0);
        if(count>=3u)s->mask[y*w+x]=2;
    }
    for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)s->mask[y*w+x]=(uint8_t)(s->mask[y*w+x]==2);
    memset(s->projection_y,0,sizeof(s->projection_y));
    for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)if(digit_ink(s,w,x,y,excluded))++s->projection_y[y];
    for(y=y0;y<=y1+gap+1u;++y) {
        int lit=y<=y1&&s->projection_y[y]>=2u;
        if(lit) {if(!active){start=y;active=1;}last=y;}
        if(active && y>last+gap) {
            unsigned xx,bstart=0,blast=0,n=0,c[2]={0,0},cf[2]={0,0};box_t boxes[2];int colactive=0,bad=0;
            if(last-start+1u<12u||last-start+1u>56u){active=0;continue;}
            memset(s->projection_x,0,sizeof(s->projection_x));
            for(xx=x0;xx<=x1;++xx) {unsigned yy;for(yy=start;yy<=last;++yy)if(digit_ink(s,w,xx,yy,excluded))++s->projection_x[xx];}
            for(xx=x0;xx<=x1+2u;++xx) {
                int col=xx<=x1&&s->projection_x[xx]>=2u;
                if(col){if(!colactive){bstart=xx;colactive=1;}blast=xx;}
                if(colactive&&xx>blast+1u) {
                    box_t b;unsigned yy,xxx,score=0;int digit;
                    b.x0=bstart;b.x1=blast;b.y0=last;b.y1=start;
                    for(yy=start;yy<=last;++yy)for(xxx=bstart;xxx<=blast;++xxx)if(digit_ink(s,w,xxx,yy,excluded)) {b.y0=imin(b.y0,yy);b.y1=imax(b.y1,yy);}
                    ++diag->digit_candidates;
                    digit=decode_digit(s,f,w,h,b,excluded,&score,1);
                    if(digit<0||n>=2u) {bad=1;++diag->digit_rejected;} else {c[n]=(unsigned)digit;cf[n]=score;boxes[n]=b;++n;}
                    colactive=0;
                }
            }
            if(!bad&&n>0u) {
                unsigned score=cf[0];int value=(int)c[0];box_t b=boxes[0];
                if(n==2u) {
                    unsigned ha=boxes[0].y1-boxes[0].y0+1u,hb=boxes[1].y1-boxes[1].y0+1u;
                    unsigned sep=boxes[1].x0-boxes[0].x1-1u;
                    if(diff(boxes[0].y1,boxes[1].y1)>imax(ha,hb)/3u || sep>imax(ha,hb) || imin(ha,hb)*2u<imax(ha,hb))bad=1;
                    value=(int)(c[0]*10u+c[1]);score=imin(cf[0],cf[1]);b.x1=boxes[1].x1;b.y0=imin(b.y0,boxes[1].y0);b.y1=imax(b.y1,boxes[1].y1);
                }
                if(!bad) {if(found>=0){++diag->conflicts;return -1;}found=value;found_conf=score;*out=b;}
            }
            active=0;
        }
    }
    *confidence=found_conf;return found;
}
/* Local panel normalization is a fallback after the original template path.
 * Four dark-panel corners define a bounded bilinear sampling map. The queue
 * storage is idle during OCR and reused as a RGB565 tile, without more memory. */
typedef struct {unsigned x[4],y[4];} digit_panel_t;
static unsigned digit_isqrt(unsigned n){unsigned r=0,bit=1u<<30;while(bit>n)bit>>=2;while(bit){if(n>=r+bit){n-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return r;}
static unsigned digit_panel_edge(digit_panel_t *p,unsigned a,unsigned b){unsigned dx=diff(p->x[a],p->x[b]),dy=diff(p->y[a],p->y[b]);return digit_isqrt(dx*dx+dy*dy);}
static void digit_panel_point(digit_panel_t *p,unsigned u,unsigned v,unsigned uw,unsigned vh,unsigned *x,unsigned *y){
 unsigned den=uw*vh;unsigned a=(uw-u)*(vh-v),b=u*(vh-v),c=(uw-u)*v,d=u*v;
 *x=(a*p->x[0]+b*p->x[1]+c*p->x[2]+d*p->x[3]+den/2u)/den;
 *y=(a*p->y[0]+b*p->y[1]+c*p->y[2]+d*p->y[3]+den/2u)/den;
}
/* Fixed-point bilinear RGB sampling is tried only after nearest sampling
 * cannot decode the panel. It reduces the second rasterization's aliasing of
 * small tilted strokes; all colour, segment and confidence gates still apply. */
static uint16_t digit_panel_sample(const uint16_t *f,unsigned w,unsigned h,
 digit_panel_t *p,unsigned u,unsigned v,unsigned uw,unsigned vh){
 unsigned den=uw*vh,a=(uw-u)*(vh-v),b=u*(vh-v),c=(uw-u)*v,d=u*v;
 unsigned nx=a*p->x[0]+b*p->x[1]+c*p->x[2]+d*p->x[3];
 unsigned ny=a*p->y[0]+b*p->y[1]+c*p->y[2]+d*p->y[3];
 unsigned x=nx/den,y=ny/den,fx=(nx%den)*256u/den,fy=(ny%den)*256u/den;
 unsigned x1=imin(w-1u,x+1u),y1=imin(h-1u,y+1u);
 uint16_t q[4]={f[y*w+x],f[y*w+x1],f[y1*w+x],f[y1*w+x1]};
 unsigned weights[4]={(256u-fx)*(256u-fy),fx*(256u-fy),(256u-fx)*fy,fx*fy};
 unsigned k,r=0,g=0,bl=0;
 for(k=0;k<4u;++k){r+=((q[k]>>11)&31u)*weights[k];g+=((q[k]>>5)&63u)*weights[k];bl+=(q[k]&31u)*weights[k];}
 return (uint16_t)((((r+32768u)>>16)<<11)|(((g+32768u)>>16)<<5)|((bl+32768u)>>16));
}
static int find_digits(traffic_vision_t *s,const uint16_t *f,unsigned w,unsigned h,
 box_t lamp,unsigned color,box_t *out,unsigned *confidence,traffic_vision_diagnostics_t *diag){
 unsigned before=diag->conflicts,cf=0,d=imax(lamp.x1-lamp.x0+1u,lamp.y1-lamp.y0+1u),x,y,n=0,j;
 unsigned x0=lamp.x0>4u*d?lamp.x0-4u*d:0u,y0=lamp.y0>2u*d?lamp.y0-2u*d:0u;
 unsigned x1=imin(w-1u,lamp.x1+6u*d),y1=imin(h-1u,lamp.y1+4u*d);
 digit_panel_t panels[8];int found=-1,value=find_digits_base(s,f,w,h,lamp,color,out,confidence,diag,0);
 if(value>=0 || diag->conflicts!=before)return value;
 memset(s->mask,0,w*h);
 for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)if(bright(f[y*w+x])<=80u)s->mask[y*w+x]=1;
 for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x)if(s->mask[y*w+x]){
  unsigned head=0,tail=0,area=0,k;box_t bb={x,y,x,y};digit_panel_t p;
  int lo_sum=(int)(x+y),hi_diff=(int)x-(int)y,lo_diff=hi_diff,hi_sum=lo_sum;
  for(k=0;k<4u;++k){p.x[k]=x;p.y[k]=y;}s->mask[y*w+x]=0;s->queue[tail++]=(y<<16)|x;
  while(head<tail){unsigned q=s->queue[head++],xx=q&65535u,yy=q>>16,idx=yy*w+xx;int sum=(int)(xx+yy),delta=(int)xx-(int)yy;
   ++area;bb.x0=imin(bb.x0,xx);bb.x1=imax(bb.x1,xx);bb.y0=imin(bb.y0,yy);bb.y1=imax(bb.y1,yy);
   if(sum<lo_sum){lo_sum=sum;p.x[0]=xx;p.y[0]=yy;}if(delta>hi_diff){hi_diff=delta;p.x[1]=xx;p.y[1]=yy;}
   if(delta<lo_diff){lo_diff=delta;p.x[2]=xx;p.y[2]=yy;}if(sum>hi_sum){hi_sum=sum;p.x[3]=xx;p.y[3]=yy;}
   if(xx>x0&&s->mask[idx-1u]){s->mask[idx-1u]=0;s->queue[tail++]=q-1u;}if(xx<x1&&s->mask[idx+1u]){s->mask[idx+1u]=0;s->queue[tail++]=q+1u;}
   if(yy>y0&&s->mask[idx-w]){s->mask[idx-w]=0;s->queue[tail++]=q-65536u;}if(yy<y1&&s->mask[idx+w]){s->mask[idx+w]=0;s->queue[tail++]=q+65536u;}
  }
  if(area<100u||bb.x0==x0||bb.x1==x1||bb.y0==y0||bb.y1==y1)continue;
  if(!(bb.x1<lamp.x0||bb.x0>lamp.x1||bb.y1<lamp.y0||bb.y0>lamp.y1))continue;
  {unsigned top=digit_panel_edge(&p,0,1),bottom=digit_panel_edge(&p,2,3),left=digit_panel_edge(&p,0,2),right=digit_panel_edge(&p,1,3);
   if(imin(top,bottom)<10u||imin(left,right)<16u||imax(top,bottom)>128u||imax(left,right)>80u)continue;
   if(imin(top,bottom)*4u<imax(top,bottom)*3u||imin(left,right)*4u<imax(left,right)*3u)continue;
   if(diff(p.y[1],p.y[0])*2u>diff(p.x[1],p.x[0])||diff(p.x[2],p.x[0])*2u>diff(p.y[2],p.y[0]))continue;
   if(n>=8u){++diag->conflicts;return -1;}panels[n++]=p;
  }
 }
 for(j=0;j<n;++j){
  digit_panel_t *p=&panels[j];unsigned pw=imax(digit_panel_edge(p,0,1),digit_panel_edge(p,2,3))+1u,ph=imax(digit_panel_edge(p,0,2),digit_panel_edge(p,1,3))+1u;
  uint16_t *tile=s->ocr_tile;box_t db;unsigned dc;
  {unsigned sampling;
   for(sampling=0;sampling<2u;++sampling){
    for(y=0;y<ph;++y)for(x=0;x<pw;++x){
     if(sampling)tile[y*pw+x]=digit_panel_sample(f,w,h,p,x,y,pw-1u,ph-1u);
     else {unsigned sx,sy;digit_panel_point(p,x,y,pw-1u,ph-1u,&sx,&sy);tile[y*pw+x]=f[sy*w+sx];}
    }
    value=find_digits_base(s,tile,pw,ph,lamp,color,&db,&dc,diag,1);
    if(diag->conflicts!=before)return -1;
    if(value>=0)break;
   }
  }
  if(value>=0){
   unsigned k;box_t mapped={w,h,0,0};
   for(k=0;k<4u;++k){unsigned sx,sy;digit_panel_point(p,k&1u?db.x1:db.x0,k&2u?db.y1:db.y0,pw-1u,ph-1u,&sx,&sy);mapped.x0=imin(mapped.x0,sx);mapped.x1=imax(mapped.x1,sx);mapped.y0=imin(mapped.y0,sy);mapped.y1=imax(mapped.y1,sy);}
   if(found>=0){++diag->conflicts;return -1;}found=value;cf=dc;*out=mapped;
  }
 }
 *confidence=cf;return found;
}
int traffic_vision_process(traffic_vision_t *s,const uint16_t *f,unsigned w,unsigned h,traffic_vision_result_t *r) {
    unsigned i,n,x,y,x0=0,y0=0,x1,y1,best=0,color=0,seen=0,ambiguous=0;box_t chosen={0,0,0,0};
    if(r==0) return -1;
    empty_result(r);
    if(s==0)return -1;
    ++s->frame_number;r->diagnostics.frame_number=s->frame_number;
    if(f==0||w<16u||h<16u||w>TRAFFIC_VISION_MAX_WIDTH||h>TRAFFIC_VISION_MAX_HEIGHT){traffic_vision_reset(s);return -1;}
    n=w*h;x1=w-1u;y1=h-1u;
    if(s->roi.w&&s->roi.h){x0=imin(s->roi.x,w);y0=imin(s->roi.y,h);x1=imin(w,(unsigned)s->roi.x+s->roi.w)-1u;y1=imin(h,(unsigned)s->roi.y+s->roi.h)-1u;}
    memset(s->mask,0,n);
    if(x0>=w||y0>=h||x0>x1||y0>y1){traffic_vision_reset(s);return 0;}
    for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x){unsigned c=classify(f[y*w+x]);s->mask[y*w+x]=(uint8_t)c;r->diagnostics.classified_pixels+=(c!=0);}
    for(i=0;i<n;++i)if(s->mask[i]) {
        unsigned original=s->mask[i],c=lamp_family(original),head=0,tail=0,area=0,sx=0,sy=0,score=0,detected,mono=0,warm_types=1u<<original;box_t b;int ok;
        b.x0=b.x1=i%w;b.y0=b.y1=i/w;s->mask[i]=0;s->queue[tail++]=(b.y0<<16)|b.x0;
        ++r->diagnostics.components;
        while(head<tail) {
            unsigned q=s->queue[head++],xx=q&65535u,yy=q>>16,p=yy*w+xx;
            ++area;sx+=xx;sy+=yy;b.x0=imin(b.x0,xx);b.x1=imax(b.x1,xx);b.y0=imin(b.y0,yy);b.y1=imax(b.y1,yy);
            /* Packed x/y queue avoids software division for every flood-fill
             * pixel on Cortex-A9; the checked frame dimensions fit 16 bits. */
            if(xx>0 && lamp_family(s->mask[p-1u])==c){warm_types|=1u<<s->mask[p-1u];s->mask[p-1u]=0;s->queue[tail++]=q-1u;}
            if(xx+1u<w && lamp_family(s->mask[p+1u])==c){warm_types|=1u<<s->mask[p+1u];s->mask[p+1u]=0;s->queue[tail++]=q+1u;}
            if(yy>0 && lamp_family(s->mask[p-w])==c){warm_types|=1u<<s->mask[p-w];s->mask[p-w]=0;s->queue[tail++]=q-65536u;}
            if(yy+1u<h && lamp_family(s->mask[p+w])==c){warm_types|=1u<<s->mask[p+w];s->mask[p+w]=0;s->queue[tail++]=q+65536u;}
        }
        /* Run the original separate-colour lamp veto before rejecting a
         * merged outline: two touching circles can make a wide non-circle. */
        if(c==TRAFFIC_RED && warm_types==((1u<<TRAFFIC_RED)|(1u<<TRAFFIC_AMBER))) {
            mono=warm_monochrome_lamps(s,f,w,h,b,area);
            if(mono>1u){++r->diagnostics.conflicts;traffic_vision_reset(s);return 0;}
        }
        if(b.x1-b.x0+1u>s->max_lamp_diameter||b.y1-b.y0+1u>s->max_lamp_diameter){++r->diagnostics.rejected_shape;if(mono)ambiguous=1;continue;}
        ok=(b.x1-b.x0+1u<s->min_lamp_diameter||b.y1-b.y0+1u<s->min_lamp_diameter)?0:
            lamp_shape(f,w,h,b,area,sx,sy,c==TRAFFIC_RED?TRAFFIC_UNKNOWN:c,&score);
        if(ok==0&&lamp_recover_glare(s,f,w,h,i,c,&b,&area,&sx,&sy,&score))ok=1;
        if(ok<=0){if(ok<0)++r->diagnostics.rejected_background;else ++r->diagnostics.rejected_shape;if(mono)ambiguous=1;continue;}
        if(score<s->min_confidence){if(mono)ambiguous=1;continue;}
        detected=lamp_color(f,w,b,c,area);
        if(!detected){ambiguous=1;++r->diagnostics.conflicts;continue;}
        ++r->diagnostics.lamp_candidates;seen|=1u<<detected;
        if(score>best){best=score;color=detected;chosen=b;}
    }
    if(ambiguous || r->diagnostics.lamp_candidates>1u || (seen && (seen&(seen-1u)))){++r->diagnostics.conflicts;traffic_vision_reset(s);return 0;}
    if(!color){traffic_vision_reset(s);return 0;}
    /* Confirmation belongs to one nearby target, not just a repeated colour.
     * A target swap must clear both streaks even if the new lamp is red too. */
    if(s->candidate_color==color && s->color_streak) {
        unsigned d=imax(s->candidate_lamp.w,s->candidate_lamp.h);
        if(diff(chosen.x0+chosen.x1,2u*s->candidate_lamp.x+s->candidate_lamp.w-1u)>2u*d ||
           diff(chosen.y0+chosen.y1,2u*s->candidate_lamp.y+s->candidate_lamp.h-1u)>2u*d ||
           diff(chosen.x1-chosen.x0+1u,s->candidate_lamp.w)>d/2u ||
           diff(chosen.y1-chosen.y0+1u,s->candidate_lamp.h)>d/2u) traffic_vision_reset(s);
    }
    if(s->candidate_color!=color){s->candidate_color=(uint8_t)color;s->color_streak=1;s->candidate_digit=-1;s->digit_streak=0;s->digit_misses=0;}
    else if(s->color_streak<255u)++s->color_streak;
    s->candidate_lamp=bbox(chosen);
    {
        box_t db={0,0,0,0};unsigned dc=0;int value=find_digits(s,f,w,h,chosen,color,&db,&dc,&r->diagnostics);
        if(value<0||dc<s->min_confidence){
            /* One unreadable frame may interrupt the two fresh observations.
             * It still emits UNKNOWN immediately. Never retain through a
             * competing display, target swap, missing lamp or second miss. */
            if(!r->diagnostics.conflicts&&s->candidate_digit>=0&&s->digit_streak&&
               !s->digit_misses&&s->color_streak>=imax(1u,s->color_confirm_frames))s->digit_misses=1;
            else {s->candidate_digit=-1;s->digit_streak=0;s->digit_misses=0;}
        } else {
            if(s->candidate_digit!=value){s->candidate_digit=(int16_t)value;s->digit_streak=1;}
            else if(s->digit_streak<255u)++s->digit_streak;
            s->digit_misses=0;
        }
        if(s->color_streak>=imax(1u,s->color_confirm_frames)){
            r->color=(uint8_t)color;r->valid=1;r->confidence=(uint8_t)best;r->lamp_bbox=bbox(chosen);
            if(value>=0&&s->digit_streak>=imax(1u,s->digit_confirm_frames)) {r->countdown=(int16_t)value;r->countdown_valid=1;r->countdown_confidence=(uint8_t)dc;r->digit_bbox=bbox(db);}
        }
    }
    return 0;
}
