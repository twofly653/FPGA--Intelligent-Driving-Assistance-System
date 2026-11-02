#include "app_features.h"
#include "pc_net_service.h"
#if APP_ENABLE_PC_NETWORK
#include "pc_net_protocol.h"
#include "pc_net_guard.h"
#include "traffic_service.h"
#include "obstacle_service.h"
#include "gem_poll.h"
#include "lwip/init.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#include "lwip/etharp.h"
#include "xtime_l.h"
#include "xil_printf.h"
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>
#include "pc_net_config.h"
#ifndef APP_PC_BOARD_IP
#define APP_PC_BOARD_IP "192.168.2.99"
#endif
#ifndef APP_PC_HOST_IP
#define APP_PC_HOST_IP "192.168.2.1"
#endif
static struct netif iface;
static struct udp_pcb *udp;
static ip_addr_t peer;
static uint8_t frame_data[320u*240u*2u],packet[PN_HEADER+PN_CHUNK];
static pn_header_t tx;
static pn_guard_t guard;
static unsigned active,last_offer,have_offer,last_flags;
static uint64_t heartbeat,last_link,last_arp,tx_started;
static uint32_t epoch=1;
static jmp_buf network_fault;
static unsigned guarded,failed;
static uint64_t now_us(void){XTime t;XTime_GetTime(&t);return (uint64_t)(t/COUNTS_PER_SECOND)*1000000u+(uint64_t)((t%COUNTS_PER_SECOND)*1000000u/COUNTS_PER_SECOND);}
u32_t sys_now(void){return (u32_t)(now_us()/1000u);}
void pc_net_assert(const char*message){
 xil_printf("[NET] assert %s; network disabled until restart\r\n",message);active=0;udp=0;failed=1;
 if(guarded)longjmp(network_fault,1);
 abort(); /* An assert must never return into inconsistent lwIP state. */
}
static void receive(void*arg,struct udp_pcb*pcb,struct pbuf*p,const ip_addr_t*from,u16_t port){
 pn_header_t h;pn_box_t boxes[8];unsigned count;uint16_t proc;uint64_t t=now_us();(void)arg;(void)pcb;
 if(!p)return;
 if(port!=APP_PC_HOST_PORT||!ip_addr_cmp(from,&peer)||p->tot_len>sizeof packet){pbuf_free(p);return;}
 pbuf_copy_partial(p,packet,p->tot_len,0);
 if(!pn_unpack(packet,p->tot_len,&h)){pbuf_free(p);return;}
 pbuf_free(p);
 if(h.type==PN_HELLO&&h.length==0u&&h.width==0u&&h.height==0u){
  if(h.session!=guard.session){++epoch;pn_guard_reset(&guard,h.session,epoch,last_flags);active=0;have_offer=0;obstacle_service_remote_clear();}
  heartbeat=t;return;
 }
 if(h.type==PN_RESULT&&pn_result_read(packet+PN_HEADER,h.length,320,240,boxes,&count,&proc)&&pn_guard_accept(&guard,&h,t))
  obstacle_service_remote_apply(boxes,count,h.frame,h.capture_us,proc);
}
static void initialize(void){
 ip4_addr_t local,mask,gateway;uint8_t address[6]={2,0x50,0x59,0x4e,0x51,0x14};
 memset(&iface,0,sizeof iface);udp=0;active=have_offer=0;heartbeat=last_link=last_arp=0;last_flags=1;epoch=1;
 pn_guard_reset(&guard,0,epoch,1);lwip_init();
 if(!ip4addr_aton(APP_PC_BOARD_IP,&local)||!ipaddr_aton(APP_PC_HOST_IP,&peer))return;
 IP4_ADDR(&mask,255,255,255,0);IP4_ADDR(&gateway,0,0,0,0);memcpy(iface.hwaddr,address,6);iface.hwaddr_len=6;
 if(!netif_add(&iface,&local,&mask,&gateway,0,gem_poll_init,ethernet_input)){xil_printf("[NET] init failed\r\n");return;}
 netif_set_default(&iface);netif_set_up(&iface);udp=udp_new();if(!udp)return;
 if(udp_bind(udp,IP_ADDR_ANY,APP_PC_BOARD_PORT)!=ERR_OK){udp_remove(udp);udp=0;return;}
 udp_recv(udp,receive,0);xil_printf("[NET] UDP %s:%u -> %s:%u; WAIT LINK/HELLO\r\n",APP_PC_BOARD_IP,APP_PC_BOARD_PORT,APP_PC_HOST_IP,APP_PC_HOST_PORT);
}
static void poll(void){
 traffic_frame_snapshot_t f;uint64_t t=now_us(),started=t;unsigned flags,k;
 if(!udp)return;
 if(t-last_link>=100000u){last_link=t;gem_poll_link(&iface);}
 gem_poll_input(&iface,8);
 if(t-last_arp>=1000000u){last_arp=t;etharp_tmr();}
 flags=obstacle_service_is_enabled()&&!obstacle_service_is_paused();
 if(flags!=last_flags){last_flags=flags;++epoch;pn_guard_reset(&guard,guard.session,epoch,flags);active=0;have_offer=0;obstacle_service_remote_clear();}
 /* Heartbeat age must be an unsigned-safe subtraction. receive() runs inside
  * gem_poll_input() below and stamps heartbeat with a now_us() sampled after
  * this poll's t, so a plain t-heartbeat wraps to ~2^64 for exactly the poll
  * that carried the HELLO and expires the session every time. t<=heartbeat is
  * a fresh stamp, never an expiry. */
 if(!netif_is_link_up(&iface)||!heartbeat||(t>heartbeat&&t-heartbeat>1500000u)){
  if(guard.session){pn_guard_reset(&guard,0,++epoch,flags);obstacle_service_remote_clear();}active=0;return;
 }
 /* A frame already being sent owns this immutable copy. Finish it in <=150ms
  * or drop it; never retain pointers into the traffic producer across polls. */
 /* capture_us comes from the PL and may sit slightly ahead of t; compare both
  * ages only when the stamp is genuinely older, or the wrap drops the frame
  * mid-transmission. */
 if(active&&((t>tx_started&&t-tx_started>150000u)||(t>tx.capture_us&&t-tx.capture_us>350000u)))active=0;
 if(!active&&traffic_service_peek_frame(&f)&&(!have_offer||last_offer!=f.sequence)){
  last_offer=f.sequence;have_offer=1;
  if(t<f.capture_us||t-f.capture_us>200000u)return;
  memcpy(frame_data,f.pixels,sizeof frame_data);
  memset(&tx,0,sizeof tx);tx.type=PN_FRAME;tx.flags=(uint16_t)flags;tx.session=guard.session;tx.epoch=epoch;
  tx.frame=f.sequence;tx.capture_us=f.capture_us;tx.width=320;tx.height=240;
  tx.chunks=(sizeof frame_data+PN_CHUNK-1u)/PN_CHUNK;tx.total=sizeof frame_data;active=1;tx_started=t;
  pn_guard_issue(&guard,tx.frame,tx.capture_us);
 }
 for(k=0;active&&k<8u&&now_us()-started<1500u;++k){
  struct pbuf*p;unsigned offset=tx.index*PN_CHUNK,n=sizeof frame_data-offset;size_t bytes;err_t result;
  if(n>PN_CHUNK)n=PN_CHUNK;
  tx.length=(uint16_t)n;bytes=pn_pack(packet,sizeof packet,&tx,frame_data+offset);
  p=pbuf_alloc(PBUF_TRANSPORT,(u16_t)bytes,PBUF_RAM);if(!p)return;
  if(pbuf_take(p,packet,(u16_t)bytes)!=ERR_OK){pbuf_free(p);return;}
  result=udp_sendto(udp,p,&peer,APP_PC_HOST_PORT);pbuf_free(p);if(result!=ERR_OK)return;
  if(++tx.index==tx.chunks)active=0;
 }
}
void pc_net_service_init(void){
 failed=0;
 if(setjmp(network_fault)){guarded=0;obstacle_service_remote_clear();return;}
 guarded=1;initialize();guarded=0;
}
void pc_net_service_poll(void){
 if(failed)return;
 if(setjmp(network_fault)){guarded=0;obstacle_service_remote_clear();return;}
 guarded=1;poll();guarded=0;
}
#else
void pc_net_service_init(void){}
void pc_net_service_poll(void){}
#endif
