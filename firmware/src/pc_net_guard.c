#include "pc_net_guard.h"
#include <string.h>
void pn_guard_reset(pn_guard_t*g,uint32_t session,uint32_t epoch,unsigned enabled){memset(g,0,sizeof *g);g->session=session;g->epoch=epoch;g->enabled=enabled;}
void pn_guard_issue(pn_guard_t*g,uint32_t frame,uint64_t capture){g->tickets[g->next%8u].frame=frame;g->tickets[g->next%8u].capture_us=capture;++g->next;}
int pn_guard_accept(pn_guard_t*g,const pn_header_t*h,uint64_t now){
 unsigned i;if(!g->enabled||h->type!=PN_RESULT||!h->flags||h->session!=g->session||h->epoch!=g->epoch||h->width!=320u||h->height!=240u||
 h->capture_us>now||now-h->capture_us>400000u||(g->have_last&&(int32_t)(h->frame-g->last)<=0))return 0;
 for(i=0;i<8u;++i)if(g->tickets[i].frame==h->frame&&g->tickets[i].capture_us==h->capture_us&&h->capture_us){g->last=h->frame;g->have_last=1;return 1;}
 return 0;
}
