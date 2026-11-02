#ifndef PC_NET_GUARD_H
#define PC_NET_GUARD_H
#include "pc_net_protocol.h"
typedef struct {uint32_t frame;uint64_t capture_us;} pn_ticket_t;
typedef struct {uint32_t session,epoch,last;unsigned have_last,enabled;pn_ticket_t tickets[8];unsigned next;} pn_guard_t;
void pn_guard_reset(pn_guard_t*g,uint32_t session,uint32_t epoch,unsigned enabled);
void pn_guard_issue(pn_guard_t*g,uint32_t frame,uint64_t capture_us);
int pn_guard_accept(pn_guard_t*g,const pn_header_t*h,uint64_t now_us);
#endif
