#include "obstacle_motion_core.h"

#include <string.h>

#define MW ((unsigned)OBSTACLE_MOTION_WIDTH)
#define MH ((unsigned)OBSTACLE_MOTION_HEIGHT)
#define MN ((unsigned)OBSTACLE_MOTION_PIXELS)
#define SOURCE_MAX_AGE_MS 500u
#define SOURCE_MAX_GAP_MS 750u
#define TRACK_MAX_AGE_MS 750u
#define MIN_COMPONENT_AREA 40u
#define CONFIRM_HITS 3u
#define CONFIRM_GAP_MS 450u
#define MIN_COMPONENT_SIDE 5u

static int iabs(int v) { return v < 0 ? -v : v; }
static unsigned umin(unsigned a, unsigned b) { return a < b ? a : b; }
static unsigned umax(unsigned a, unsigned b) { return a > b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void obstacle_motion_reset(obstacle_motion_t *s)
{
    if (!s) return;
    s->have_previous = 0;
    s->warmup_left = 3;
    s->candidate_count = 0;
    s->next_id = 1;
    memset(s->noise_q2, 4, sizeof(s->noise_q2));
    memset(s->tracks, 0, sizeof(s->tracks));
}

void obstacle_motion_init(obstacle_motion_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    obstacle_motion_reset(s);
}

static unsigned luma565(uint16_t p)
{
    unsigned r = (p >> 11) & 31u;
    unsigned g = (p >> 5) & 63u;
    unsigned b = p & 31u;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (77u * r + 150u * g + 29u * b + 128u) >> 8;
}

static void gaussian3(const uint8_t *src,uint8_t *dst)
{
    unsigned x,y;
    for(y=0;y<MH;++y){
        unsigned ym=y?y-1u:0u,yp=umin(y+1u,MH-1u);
        for(x=0;x<MW;++x){
            unsigned xm=x?x-1u:0u,xp=umin(x+1u,MW-1u);
            unsigned sum=src[ym*MW+xm]+2u*src[ym*MW+x]+src[ym*MW+xp]+
                2u*src[y*MW+xm]+4u*src[y*MW+x]+2u*src[y*MW+xp]+
                src[yp*MW+xm]+2u*src[yp*MW+x]+src[yp*MW+xp];
            dst[y*MW+x]=(uint8_t)((sum+8u)>>4);
        }
    }
}

static unsigned chroma565(uint16_t p,unsigned channel)
{
    unsigned g=(p>>5)&63u,c=channel?((p&31u)):((p>>11)&31u);
    g=(g<<2)|(g>>4);c=(c<<3)|(c>>2);
    return (unsigned)(((int)c-(int)g+256)/2); /* 0..255, additive-light invariant. */
}

static void make_inputs(obstacle_motion_t *s,const uint16_t *rgb)
{
    unsigned x,y,c;
    for(y=0;y<MH;++y){
        const uint16_t *a=rgb+2u*y*OBSTACLE_MOTION_INPUT_WIDTH;
        const uint16_t *b=a+OBSTACLE_MOTION_INPUT_WIDTH;
        for(x=0;x<MW;++x){
            unsigned xx=2u*x;
            s->gray[y*MW+x]=(uint8_t)((luma565(a[xx])+luma565(a[xx+1u])+
                luma565(b[xx])+luma565(b[xx+1u])+2u)>>2);
        }
    }
    gaussian3(s->gray,s->smooth);
    /* Reuse the raw-gray scratch array after its filtered luma has been saved.
     * Both opponent-colour channels are necessary for equal-luma objects and
     * preserve a coloured body across internal light/dark texture. */
    for(c=0;c<2u;++c){
        for(y=0;y<MH;++y){
            const uint16_t *a=rgb+2u*y*OBSTACLE_MOTION_INPUT_WIDTH;
            const uint16_t *b=a+OBSTACLE_MOTION_INPUT_WIDTH;
            for(x=0;x<MW;++x){
                unsigned xx=2u*x;
                s->gray[y*MW+x]=(uint8_t)((chroma565(a[xx],c)+chroma565(a[xx+1u],c)+
                    chroma565(b[xx],c)+chroma565(b[xx+1u],c)+2u)>>2);
            }
        }
        gaussian3(s->gray,s->chroma[c]);
    }
}
/* Median of signed per-pixel differences resists local moving objects occupying
 * less than half the image. old16 selects a persistent Q4 background reference. */
static int median_delta(obstacle_motion_t *s,const uint8_t *cur,const uint8_t *old8,const uint16_t *old16)
{
    unsigned i, total = 0;
    memset(s->histogram, 0, sizeof(s->histogram));
    for (i = 0; i < MN; ++i) {
        int old=old16?(int)((old16[i]+8u)>>4):old8[i];
        ++s->histogram[(int)cur[i]-old+255];
    }
    for (i = 0; i < 511u; ++i) {
        total += s->histogram[i];
        if (total >= MN / 2u) return (int)i - 255;
    }
    return 0;
}

static unsigned noise_level(obstacle_motion_t *s,const uint8_t *cur,const uint8_t *old,int delta)
{
    unsigned i, total = 0;
    memset(s->histogram, 0, sizeof(s->histogram));
    for (i = 0; i < MN; ++i) {
        unsigned e=(unsigned)iabs((int)cur[i]-old[i]-delta);
        ++s->histogram[umin(e, 255u)];
    }
    for (i = 0; i < 256u; ++i) {
        total += s->histogram[i];
        if (total >= MN / 2u) return i;
    }
    return 255u;
}

/* Robust photometric registration, independently for Y, R-G and B-G.
 * A scalar offset cannot compensate auto-exposure gain on textured scenery.
 * Fit only previous background inliers, then trim once more. Q8 integer affine
 * transforms keep the original pixel data immutable and need no float runtime. */
static int registered_value(int old,unsigned c,const int *photo)
{
    return clampi(((old*photo[c+3u]+128)>>8)+photo[c],0,255);
}
static void fit_photometric(const obstacle_motion_t *s,const uint8_t *cur,
                            const uint8_t *old8,const uint16_t *old16,
                            unsigned c,int *photo)
{
    unsigned pass,i;
    for(pass=0;pass<2u;++pass){
        int64_t sx=0,sy=0,sxx=0,sxy=0,n=0,den,num;
        int gain,offset;
        for(i=0;i<MN;i+=4u){
            int old=old16?(int)((old16[i]+8u)>>4):old8[i],v=cur[i];
            if(s->previous_foreground[i] ||
               iabs(v-registered_value(old,c,photo))>(pass?12:24))continue;
            sx+=old;sy+=v;sxx+=old*old;sxy+=old*v;++n;
        }
        if(n<(int64_t)(MN/16u))return;
        den=n*sxx-sx*sx;num=n*sxy-sx*sy;
        /* Flat scenery cannot constrain gain: retain robust median offset. */
        if(den<n*n*36 || num<=0)return;
        gain=(int)((num*256+den/2)/den);
        if(gain<160 || gain>400)return;
        offset=(int)((sy*256-gain*sx)/(n*256));
        photo[c+3u]=gain;photo[c]=offset;
    }
}
static unsigned local_threshold(const obstacle_motion_t *s,unsigned c,unsigned i,unsigned base)
{
    return umax(base,umin(c?36u:56u,6u+(3u*s->noise_q2[c][i]+2u)/4u));
}
static unsigned colour_aware_error(const obstacle_motion_t *s,unsigned i,unsigned p,const int *photo)
{
    unsigned e=(unsigned)iabs((int)s->smooth[i]-registered_value(s->previous[p],0,photo));
    unsigned c;
    for(c=0;c<2u;++c)e=umax(e,2u*(unsigned)iabs((int)s->chroma[c][i]-registered_value(s->previous_chroma[c][p],c+1u,photo)));
    return e;
}
static int current_motion(const obstacle_motion_t *s,unsigned i,const int *photo,
                          unsigned threshold,unsigned chroma_threshold)
{
    unsigned c;
    if((unsigned)iabs((int)s->smooth[i]-registered_value(s->previous[i],0,photo))>=local_threshold(s,0,i,threshold))return 1;
    for(c=0;c<2u;++c)
        if((unsigned)iabs((int)s->chroma[c][i]-registered_value(s->previous_chroma[c][i],c+1u,photo))>=local_threshold(s,c+1u,i,chroma_threshold))return 1;
    return 0;
}
/* Distributed coherent shift of previously known BACKGROUND pixels. A moving
 * textured object can cross all four quadrants, so quadrant voting alone is
 * insufficient. Previously foreground pixels and their shifted samples are
 * excluded; >=3 sufficiently textured background quadrants must agree.
 * This is a conservative guard, not stabilisation or visual odometry. */
static int coherent_camera_shift(const obstacle_motion_t *s,const int *delta)
{
    unsigned x,y;
    int dx,dy;
    for (dy=-2;dy<=2;++dy) {
        for (dx=-2;dx<=2;++dx) {
            unsigned base[4]={0,0,0,0},shifted[4]={0,0,0,0},n[4]={0,0,0,0};
            unsigned good=0,q;
            if (!dx && !dy) continue;
            for (y=4;y+4u<MH;y+=4u) {
                for (x=4;x+4u<MW;x+=4u) {
                    unsigned i=y*MW+x;
                    unsigned p=(unsigned)((int)y+dy)*MW+(unsigned)((int)x+dx);
                    if (s->previous_foreground[i] || s->previous_foreground[p]) continue;
                    q=(x>=MW/2u?1u:0u)+(y>=MH/2u?2u:0u);
                    base[q]+=colour_aware_error(s,i,i,delta);
                    shifted[q]+=colour_aware_error(s,i,p,delta);
                    ++n[q];
                }
            }
            for (q=0;q<4u;++q)
                if (n[q]>=120u && base[q]>=5u*n[q] && shifted[q]*5u<base[q]*3u) ++good;
            if (good>=3u) return 1;
        }
    }
    return 0;
}
static void learn_current(obstacle_motion_t *s)
{
    unsigned i,c;
    if(s->have_previous){
        for(i=0;i<MN;++i){
            unsigned e=(unsigned)iabs((int)s->smooth[i]-s->previous[i]);
            s->noise_q2[0][i]=(uint8_t)umin(80u,umax(s->noise_q2[0][i],e*4u));
            for(c=0;c<2u;++c){
                e=(unsigned)iabs((int)s->chroma[c][i]-s->previous_chroma[c][i]);
                s->noise_q2[c+1u][i]=(uint8_t)umin(48u,umax(s->noise_q2[c+1u][i],e*4u));
            }
        }
    }
    for(i=0;i<MN;++i)s->background_q4[i]=(uint16_t)(s->smooth[i]<<4);
    memcpy(s->previous,s->smooth,sizeof(s->previous));
    for(c=0;c<2u;++c){
        for(i=0;i<MN;++i)s->background_chroma_q4[c][i]=(uint16_t)(s->chroma[c][i]<<4);
        memcpy(s->previous_chroma[c],s->chroma[c],sizeof(s->previous_chroma[c]));
    }
    memset(s->previous_foreground,0,sizeof(s->previous_foreground));
}
/* 3x3 binary opening suppresses isolated pixels without closing the clear gap
 * between neighbouring objects. Components touching frame edges remain legal. */
static void open_mask(obstacle_motion_t *s)
{
    unsigned x, y;
    memset(s->scratch, 0, sizeof(s->scratch));
    for (y = 1; y + 1u < MH; ++y) {
        for (x = 1; x + 1u < MW; ++x) {
            unsigned i = y * MW + x;
            s->scratch[i] = (uint8_t)(s->mask[i-MW-1u] && s->mask[i-MW] &&
                s->mask[i-MW+1u] && s->mask[i-1u] && s->mask[i] && s->mask[i+1u] &&
                s->mask[i+MW-1u] && s->mask[i+MW] && s->mask[i+MW+1u]);
        }
    }
    memset(s->mask, 0, sizeof(s->mask));
    for (y = 1; y + 1u < MH; ++y) {
        for (x = 1; x + 1u < MW; ++x) {
            unsigned i = y * MW + x;
            if (s->scratch[i]) {
                s->mask[i-MW-1u]=s->mask[i-MW]=s->mask[i-MW+1u]=1;
                s->mask[i-1u]=s->mask[i]=s->mask[i+1u]=1;
                s->mask[i+MW-1u]=s->mask[i+MW]=s->mask[i+MW+1u]=1;
            }
        }
    }
}

static void keep_component(obstacle_motion_t *s, const obstacle_motion_component_t *c)
{
    unsigned pos = 0, j, count = s->candidate_count;
    while (pos < count && s->candidates[pos].area >= c->area) ++pos;
    if (pos >= OBSTACLE_MOTION_MAX_CANDIDATES) return;
    if (count < OBSTACLE_MOTION_MAX_CANDIDATES) ++count;
    for (j = count - 1u; j > pos; --j) s->candidates[j] = s->candidates[j-1u];
    s->candidates[pos] = *c;
    s->candidate_count = (uint8_t)count;
}

/* A changing shadow/reflection preserves background texture under a local
 * affine brightness mapping. Reject it only when textured evidence supports
 * that mapping; flat dark objects are not blindly classified as shadows. */
static int lighting_only(const obstacle_motion_t *s,unsigned area,const int *photo)
{
    int64_t sx=0,sy=0,sxx=0,syy=0,sxy=0,vx,vy,cov,den;
    unsigned j,n=0,colour_changes=0;
    for(j=0;j<area;++j){
        unsigned i=s->queue[j],c,px=i%MW,py=i/MW,xx,yy;
        int interior=1;
        int x,y;
        /* Exclude blurred foreground boundaries from the texture test. A
         * shadow edge mixes lit and unlit pixels and is not one affine map. */
        if(px<2u || py<2u || px+2u>=MW || py+2u>=MH)continue;
        for(yy=py-2u;yy<=py+2u;++yy)for(xx=px-2u;xx<=px+2u;++xx)
            if(!s->foreground[yy*MW+xx])interior=0;
        if(!interior)continue;
        ++n;
        x=registered_value((s->background_q4[i]+8u)>>4,0,photo);y=s->smooth[i];
        sx+=x;sy+=y;sxx+=x*x;syy+=y*y;sxy+=x*y;
        for(c=0;c<2u;++c){
            int old=(s->background_chroma_q4[c][i]+8u)>>4;
            if(iabs((int)s->chroma[c][i]-registered_value(old,c+1u,photo))>12){++colour_changes;break;}
        }
    }
    if(n<32u || n<area/4u || colour_changes*10u>n)return 0;
    vx=(int64_t)n*sxx-sx*sx;vy=(int64_t)n*syy-sy*sy;
    cov=(int64_t)n*sxy-sx*sy;
    if(vx<(int64_t)n*n*64 || vy<(int64_t)n*n*16 || cov<=0)return 0;
    /* Scale down before products: bounded int64 even at 19,200 pixels. */
    vx/=n;vy/=n;cov/=n;den=vx*vy;
    return cov*cov>=den-den*3/100 && cov*2>=vx && cov*2<=vx*3;
}
static void components(obstacle_motion_t *s,obstacle_motion_result_t *r,const int *delta,
                       const int *bg_photo,unsigned threshold,unsigned chroma_threshold)
{
    unsigned seed;
    s->candidate_count = 0;
    for (seed = 0; seed < MN; ++seed) {
        unsigned head = 0, tail = 0, xmin, xmax, ymin, ymax, area = 0, support = 0;
        obstacle_motion_component_t c;
        if (!s->mask[seed]) continue;
        s->mask[seed] = 0;
        s->queue[tail++] = (uint16_t)seed;
        xmin = xmax = seed % MW;
        ymin = ymax = seed / MW;
        while (head < tail) {
            unsigned p = s->queue[head++], x = p % MW, y = p / MW;
            unsigned xx, yy, xa = x ? x - 1u : 0u, ya = y ? y - 1u : 0u;
            unsigned xb = umin(x + 1u, MW - 1u), yb = umin(y + 1u, MH - 1u);
            ++area;
            if(current_motion(s,p,delta,threshold,chroma_threshold))++support;
            xmin = umin(xmin,x); xmax = umax(xmax,x);
            ymin = umin(ymin,y); ymax = umax(ymax,y);
            for (yy = ya; yy <= yb; ++yy) {
                for (xx = xa; xx <= xb; ++xx) {
                    unsigned k = yy * MW + xx;
                    if (s->mask[k]) {
                        s->mask[k] = 0;
                        /* Every pixel enqueues at most once; queue has MN entries. */
                        s->queue[tail++] = (uint16_t)k;
                    }
                }
            }
        }
        ++r->components;
        if (area < MIN_COMPONENT_AREA || xmax-xmin+1u < MIN_COMPONENT_SIDE ||
            ymax-ymin+1u < MIN_COMPONENT_SIDE || support < umax(8u,area/40u)) continue;
        /* Extremely thin sparse components are usually illumination/aliasing edges. */
        if (area * 6u < (xmax-xmin+1u) * (ymax-ymin+1u)) continue;
        if (lighting_only(s,area,bg_photo)) continue;
        memset(&c, 0, sizeof(c));
        c.bbox.x = (uint16_t)xmin; c.bbox.y = (uint16_t)ymin;
        c.bbox.w = (uint16_t)(xmax-xmin+1u); c.bbox.h = (uint16_t)(ymax-ymin+1u);
        c.area = (uint16_t)area; c.motion_pixels = (uint16_t)support;
        {
            unsigned j,k,sum[2]={0,0};
            for(j=0;j<area;++j)for(k=0;k<2u;++k)sum[k]+=s->chroma[k][s->queue[j]];
            for(k=0;k<2u;++k)c.chroma_mean[k]=(uint8_t)(sum[k]/area);
        }
        keep_component(s, &c);
    }
}

static unsigned pair_cost(const obstacle_motion_track_t *t, const obstacle_motion_component_t *c)
{
    int tx = 2*(int)t->bbox.x + t->bbox.w + 2*t->vx;
    int ty = 2*(int)t->bbox.y + t->bbox.h + 2*t->vy;
    int cx = 2*(int)c->bbox.x + c->bbox.w;
    int cy = 2*(int)c->bbox.y + c->bbox.h;
    unsigned d = (unsigned)(iabs(tx-cx) + iabs(ty-cy));
    unsigned gate = 2u * umin(80u,umax(t->bbox.w,t->bbox.h)+24u);
    unsigned a = (unsigned)t->bbox.w*t->bbox.h, b = (unsigned)c->bbox.w*c->bbox.h;
    if (d > gate || umax(a,b) > 4u*umin(a,b)) return 0xffffffffu;
    {
        unsigned xa=umax(t->bbox.x,c->bbox.x),ya=umax(t->bbox.y,c->bbox.y);
        unsigned xb=umin(t->bbox.x+t->bbox.w,c->bbox.x+c->bbox.w);
        unsigned yb=umin(t->bbox.y+t->bbox.h,c->bbox.y+c->bbox.h);
        unsigned inter=xb>xa && yb>ya?(xb-xa)*(yb-ya):0u;
        unsigned appearance=(unsigned)(iabs((int)t->chroma_mean[0]-c->chroma_mean[0])+iabs((int)t->chroma_mean[1]-c->chroma_mean[1]));
        if(appearance>60u)return 0xffffffffu;
        return d+(umax(a,b)-umin(a,b))*32u/umax(a,b)+32u-inter*32u/(a+b-inter)+appearance/2u;
    }
}

static uint16_t allocate_id(obstacle_motion_t *s)
{
    unsigned j;
    uint16_t id;
    int collision;
    do {
        id=s->next_id++;
        if (!s->next_id) s->next_id=1;
        collision=!id;
        for (j=0;j<OBSTACLE_MOTION_MAX_OBJECTS;++j)
            if (s->tracks[j].used && s->tracks[j].id==id) collision=1;
    } while (collision); /* <=8 live IDs, therefore at most nine attempts. */
    return id;
}

static unsigned bitcount8(unsigned v)
{
    unsigned n=0;
    while(v){n+=v&1u;v>>=1;}
    return n;
}

/* Exhaustive bounded assignment DP: first maximise observed associations,
 * then minimise total gated distance. <=8 objects x 256 states x 8 tracks;
 * arrays live in DDR state, never in the small application stack. */
static void match_tracks(obstacle_motion_t *s,unsigned n,uint8_t *match)
{
    unsigned i,j,m,best_mask=0,best_count=0;
    uint32_t best_cost=0xffffffffu;
    for(m=0;m<256u;++m)s->assignment_cost[m]=0xffffffffu;
    s->assignment_cost[0]=0;
    for(i=0;i<n;++i){
        for(m=0;m<256u;++m)s->assignment_next_cost[m]=0xffffffffu;
        for(m=0;m<256u;++m){
            uint32_t old=s->assignment_cost[m];
            if(old==0xffffffffu)continue;
            if(old<s->assignment_next_cost[m]){
                s->assignment_next_cost[m]=old;
                s->assignment_path[i][m]=(uint16_t)m; /* unmatched */
            }
            for(j=0;j<OBSTACLE_MOTION_MAX_OBJECTS;++j){
                unsigned new_mask,cost;
                if((m&(1u<<j)) || !s->tracks[j].used)continue;
                cost=pair_cost(&s->tracks[j],&s->candidates[i]);
                if(cost==0xffffffffu)continue;
                new_mask=m|(1u<<j);
                if(old+cost<s->assignment_next_cost[new_mask]){
                    s->assignment_next_cost[new_mask]=old+cost;
                    s->assignment_path[i][new_mask]=(uint16_t)(m|((j+1u)<<8));
                }
            }
        }
        memcpy(s->assignment_cost,s->assignment_next_cost,sizeof(s->assignment_cost));
    }
    for(m=0;m<256u;++m){
        unsigned count=bitcount8(m);
        if(s->assignment_cost[m]!=0xffffffffu && (count>best_count ||
           (count==best_count && s->assignment_cost[m]<best_cost))){
            best_mask=m;best_count=count;best_cost=s->assignment_cost[m];
        }
    }
    for(i=n;i>0u;--i){
        unsigned path=s->assignment_path[i-1u][best_mask];
        match[i-1u]=(uint8_t)((path>>8)?(path>>8)-1u:255u);
        best_mask=path&255u;
    }
}
static void assign_and_output(obstacle_motion_t *s, obstacle_motion_result_t *r, uint32_t now)
{
    uint8_t occupied[OBSTACLE_MOTION_MAX_OBJECTS] = {0};
    uint8_t match[OBSTACLE_MOTION_MAX_OBJECTS];
    unsigned n = umin(s->candidate_count,OBSTACLE_MOTION_MAX_OBJECTS), i, j;
    memset(match, 255, sizeof(match));
    for (j = 0; j < OBSTACLE_MOTION_MAX_OBJECTS; ++j)
        if (s->tracks[j].used && now-s->tracks[j].last_seen_ms > TRACK_MAX_AGE_MS)
            s->tracks[j].used = 0;
    match_tracks(s,n,match);
    for (i=0;i<n;++i) if(match[i]!=255u) occupied[match[i]]=1;
    for (i = 0; i < n; ++i) {
        obstacle_motion_component_t *c = &s->candidates[i];
        obstacle_motion_track_t *t;
        obstacle_motion_object_t *o;
        unsigned slot = match[i];
        int fresh = slot == 255u;
        if (fresh) {
            uint32_t age_best = 0;
            unsigned tentative_best=0;
            slot = OBSTACLE_MOTION_MAX_OBJECTS;
            for (j = 0; j < OBSTACLE_MOTION_MAX_OBJECTS; ++j) {
                if (occupied[j]) continue;
                if (!s->tracks[j].used) { slot=j; break; }
                {
                    unsigned tentative=!s->tracks[j].confirmed;
                    uint32_t age=now-s->tracks[j].last_seen_ms;
                    if (slot == OBSTACLE_MOTION_MAX_OBJECTS || tentative>tentative_best ||
                        (tentative==tentative_best && age>age_best)) {
                        slot=j;age_best=age;tentative_best=tentative;
                    }
                }
            }
            if (slot == OBSTACLE_MOTION_MAX_OBJECTS) continue;
            occupied[slot] = 1;
        }
        t = &s->tracks[slot];
        if (fresh) {
            t->id=allocate_id(s); t->vx=t->vy=0;
            t->anchor=c->bbox;t->first_seen_ms=now;t->hits=0;t->confirmed=0;
        }
        else {
            int ox = 2*(int)t->bbox.x+t->bbox.w, oy = 2*(int)t->bbox.y+t->bbox.h;
            t->vx = (int16_t)clampi(((int)c->bbox.x*2+c->bbox.w-ox)/2,-40,40);
            t->vy = (int16_t)clampi(((int)c->bbox.y*2+c->bbox.h-oy)/2,-40,40);
        }
        if(!fresh && (!t->hits || now-t->last_seen_ms>CONFIRM_GAP_MS)){
            t->anchor=c->bbox;t->first_seen_ms=now;t->hits=0;t->confirmed=0;
        }
        if(t->hits<255u)++t->hits;
        if(!t->confirmed && t->hits>=CONFIRM_HITS && now-t->first_seen_ms>=80u){
            int travel=iabs(2*(int)c->bbox.x+c->bbox.w-2*(int)t->anchor.x-t->anchor.w)+
                       iabs(2*(int)c->bbox.y+c->bbox.h-2*(int)t->anchor.y-t->anchor.h);
            int scale=iabs((int)c->bbox.w-t->anchor.w)+iabs((int)c->bbox.h-t->anchor.h);
            if(travel>=6 || scale>=4)t->confirmed=1;
        }
        t->bbox=c->bbox; t->last_seen_ms=now; t->used=1;
        memcpy(t->chroma_mean,c->chroma_mean,sizeof t->chroma_mean);
        if(!t->confirmed)continue;
        o = &r->objects[r->count++];
        o->bbox.x=(uint16_t)(c->bbox.x*2u); o->bbox.y=(uint16_t)(c->bbox.y*2u);
        o->bbox.w=(uint16_t)(c->bbox.w*2u); o->bbox.h=(uint16_t)(c->bbox.h*2u);
        o->id=t->id; o->area=c->area; o->motion_pixels=c->motion_pixels;
        o->confidence=(uint8_t)umin(100u,50u+c->motion_pixels*50u/c->area);
    }
    for(j=0;j<OBSTACLE_MOTION_MAX_OBJECTS;++j){
        if(!occupied[j] && s->tracks[j].used && !s->tracks[j].confirmed)s->tracks[j].hits=0;
    }
}

int obstacle_motion_process(obstacle_motion_t *s, const uint16_t *rgb,
                            unsigned width, unsigned height, uint32_t frame_id,
                            uint32_t capture_ms, uint32_t now_ms,
                            obstacle_motion_result_t *r)
{
    unsigned i,c,changed=0,foreground=0,noise,threshold,bg_threshold,chroma_noise=0,chroma_threshold,chroma_bg_threshold;
    int delta[6]={0,0,0,256,256,256},bg_delta[6]={0,0,0,256,256,256};
    int median_luma;
    if (!r) return -1;
    memset(r,0,sizeof(*r));
    r->frame_id=frame_id; r->capture_ms=capture_ms;
    if (!s || !rgb || width!=OBSTACLE_MOTION_INPUT_WIDTH || height!=OBSTACLE_MOTION_INPUT_HEIGHT) {
        if (s) obstacle_motion_reset(s);
        r->status=OBSTACLE_MOTION_INVALID;
        return -1;
    }
    if ((uint32_t)(now_ms-capture_ms)>SOURCE_MAX_AGE_MS) {
        obstacle_motion_reset(s); r->status=OBSTACLE_MOTION_STALE; return 0;
    }
    if (s->have_previous && (int32_t)(frame_id-s->previous_frame_id)<=0) {
        r->status=OBSTACLE_MOTION_DUPLICATE; return 0;
    }
    if (s->have_previous && ((int32_t)(capture_ms-s->previous_capture_ms)<=0 ||
                            capture_ms-s->previous_capture_ms>SOURCE_MAX_GAP_MS)) {
        obstacle_motion_reset(s); r->status=OBSTACLE_MOTION_STALE; return 0;
    }
    make_inputs(s,rgb);
    if (!s->have_previous || s->warmup_left) {
        learn_current(s);
        s->have_previous=1;
        if (s->warmup_left) --s->warmup_left;
        s->previous_frame_id=frame_id; s->previous_capture_ms=capture_ms;
        r->status=OBSTACLE_MOTION_WARMUP;
        return 0;
    }
    delta[0]=median_delta(s,s->smooth,s->previous,NULL);
    bg_delta[0]=median_delta(s,s->smooth,NULL,s->background_q4);
    for(c=0;c<2u;++c){
        delta[c+1u]=median_delta(s,s->chroma[c],s->previous_chroma[c],NULL);
        bg_delta[c+1u]=median_delta(s,s->chroma[c],NULL,s->background_chroma_q4[c]);
        chroma_noise=umax(chroma_noise,noise_level(s,s->chroma[c],s->previous_chroma[c],delta[c+1u]));
    }
    noise=noise_level(s,s->smooth,s->previous,delta[0]);
    median_luma=delta[0];
    fit_photometric(s,s->smooth,s->previous,NULL,0,delta);
    fit_photometric(s,s->smooth,NULL,s->background_q4,0,bg_delta);
    for(c=0;c<2u;++c){
        fit_photometric(s,s->chroma[c],s->previous_chroma[c],NULL,c+1u,delta);
        fit_photometric(s,s->chroma[c],NULL,s->background_chroma_q4[c],c+1u,bg_delta);
    }
    threshold=(unsigned)clampi(10+3*(int)noise,12,30);
    bg_threshold=(unsigned)clampi((int)threshold+8,20,42);
    chroma_threshold=(unsigned)clampi(8+3*(int)chroma_noise,10,22);
    chroma_bg_threshold=(unsigned)clampi((int)chroma_threshold+6,16,30);
    r->illumination_delta=(int16_t)median_luma;
    r->threshold=(uint8_t)threshold; r->background_threshold=(uint8_t)bg_threshold;
    for(i=0;i<MN;++i){
        int old=(s->background_q4[i]+8u)>>4;
        unsigned fg=(unsigned)iabs((int)s->smooth[i]-registered_value(old,0,bg_delta))>=local_threshold(s,0,i,bg_threshold);
        if(current_motion(s,i,delta,threshold,chroma_threshold))++changed;
        for(c=0;c<2u;++c){
            old=(s->background_chroma_q4[c][i]+8u)>>4;
            if((unsigned)iabs((int)s->chroma[c][i]-registered_value(old,c+1u,bg_delta))>=local_threshold(s,c+1u,i,chroma_bg_threshold))fg=1;
        }
        s->mask[i]=(uint8_t)fg;foreground+=fg;
    }
    r->changed_pixels=(uint16_t)changed; r->foreground_pixels=(uint16_t)foreground;
    if (iabs(median_luma)>=18 ||
        changed*100u>MN*35u || foreground*100u>MN*60u ||
        coherent_camera_shift(s,delta)) {
        memset(s->tracks,0,sizeof(s->tracks));
        s->have_previous=0;
        memset(s->noise_q2,4,sizeof(s->noise_q2));
        learn_current(s);
        s->have_previous=1;s->warmup_left=2;
        s->previous_frame_id=frame_id; s->previous_capture_ms=capture_ms;
        r->status=OBSTACLE_MOTION_GLOBAL_CHANGE;
        return 0;
    }
    memcpy(s->foreground,s->mask,sizeof(s->foreground));
    open_mask(s);
    components(s,r,delta,bg_delta,threshold,chroma_threshold);
    assign_and_output(s,r,capture_ms);
    /* Apply global affine illumination correction to the reference. Learn
     * only background pixels: a stopped foreground object must not burn in and
     * leave a persistent vacated-position ghost when it subsequently leaves. */
    for (i=0;i<MN;++i) {
        int bg=clampi(((int)s->background_q4[i]*bg_delta[3]+128)/256+bg_delta[0]*16,0,255*16);
        if (!s->foreground[i]) bg+=((int)s->smooth[i]*16-bg)/8;
        if(!s->foreground[i] && !s->previous_foreground[i]){
            unsigned ch;
            for(ch=0;ch<3u;++ch){
                int cur=ch?s->chroma[ch-1u][i]:s->smooth[i];
                int old=ch?s->previous_chroma[ch-1u][i]:s->previous[i];
                int dev=4*iabs(cur-registered_value(old,ch,delta));
                int q=s->noise_q2[ch][i];
                s->noise_q2[ch][i]=(uint8_t)clampi((15*q+dev+8)/16,4,ch?96:160);
            }
        }
        s->background_q4[i]=(uint16_t)bg;
        for(c=0;c<2u;++c){
            bg=clampi(((int)s->background_chroma_q4[c][i]*bg_delta[c+4u]+128)/256+bg_delta[c+1u]*16,0,255*16);
            if(!s->foreground[i])bg+=((int)s->chroma[c][i]*16-bg)/8;
            s->background_chroma_q4[c][i]=(uint16_t)bg;
        }
    }
    memcpy(s->previous_foreground,s->foreground,sizeof(s->previous_foreground));
    memcpy(s->previous,s->smooth,sizeof(s->previous));
    memcpy(s->previous_chroma,s->chroma,sizeof(s->previous_chroma));
    s->previous_frame_id=frame_id; s->previous_capture_ms=capture_ms;
    r->status=OBSTACLE_MOTION_READY;
    return 0;
}




