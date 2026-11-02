/* Polling netif for the existing AMD XEmacPs BSP. No IRQ registration or waits
 * for cable/autonegotiation. lwIP retains its own third-party license. */
#include "gem_poll.h"
#include "xparameters.h"
#include "xemacps.h"
#include "xil_cache.h"
#include "xil_mmu.h"
#include "xil_io.h"
#include "netif/etharp.h"
#include <string.h>
#define RX_N 64u
#define TX_N 32u
#define BUF 1600u
static XEmacPs mac;
/* Dedicated 1MiB mapping: changing its attributes cannot cover other state. */
static unsigned char descriptors[1024u*1024u] __attribute__((aligned(1048576)));
static unsigned char rxbuf[RX_N][BUF] __attribute__((aligned(64)));
static unsigned char txbuf[TX_N][BUF] __attribute__((aligned(64)));
static unsigned phy=32,scan,configured_speed;
static void tx_reclaim(void){
 XEmacPs_Bd *bd;unsigned n=XEmacPs_BdRingFromHwTx(&mac.TxBdRing,TX_N,&bd);
 if(n) (void)XEmacPs_BdRingFree(&mac.TxBdRing,n,bd);
}
static err_t output(struct netif *n,struct pbuf *p){
 XEmacPs_Bd *bd;unsigned i;int result;(void)n;
 if(!netif_is_link_up(n)||p->tot_len>1514)return ERR_IF;
 tx_reclaim();result=XEmacPs_BdRingAlloc(&mac.TxBdRing,1,&bd);if(result!=XST_SUCCESS)return ERR_MEM;
 i=((UINTPTR)bd-mac.TxBdRing.BaseBdAddr)/mac.TxBdRing.Separation;
 if(i>=TX_N)return ERR_IF;
 pbuf_copy_partial(p,txbuf[i],p->tot_len,0);
 Xil_DCacheFlushRange((INTPTR)txbuf[i],BUF);
 XEmacPs_BdSetStatus(bd,(XEmacPs_BdIsTxWrap(bd)?XEMACPS_TXBUF_WRAP_MASK:0u)|XEMACPS_TXBUF_USED_MASK);
 XEmacPs_BdSetAddressTx(bd,(UINTPTR)txbuf[i]);XEmacPs_BdSetLength(bd,p->tot_len);XEmacPs_BdSetLast(bd);
 dsb();XEmacPs_BdClearTxUsed(bd);dsb();
 if(XEmacPs_BdRingToHw(&mac.TxBdRing,1,bd)!=XST_SUCCESS)return ERR_IF;
 XEmacPs_Transmit(&mac);return ERR_OK;
}
err_t gem_poll_init(struct netif*n){
 XEmacPs_Config *cfg=XEmacPs_LookupConfig(XPAR_XEMACPS_0_DEVICE_ID);
 XEmacPs_Bd template,*bd,*first;unsigned i;
 if(!cfg||XEmacPs_CfgInitialize(&mac,cfg,cfg->BaseAddress)!=XST_SUCCESS)return ERR_IF;
 XEmacPs_SetMdioDivisor(&mac,MDC_DIV_224);
 n->hwaddr_len=6;(void)XEmacPs_SetMacAddress(&mac,n->hwaddr,1);
 (void)XEmacPs_ClearOptions(&mac,XEMACPS_RX_CHKSUM_ENABLE_OPTION|XEMACPS_TX_CHKSUM_ENABLE_OPTION|XEMACPS_JUMBO_ENABLE_OPTION);
 (void)XEmacPs_SetOptions(&mac,XEMACPS_FCS_STRIP_OPTION|XEMACPS_FCS_INSERT_OPTION|XEMACPS_BROADCAST_OPTION|XEMACPS_TRANSMITTER_ENABLE_OPTION|XEMACPS_RECEIVER_ENABLE_OPTION);
 Xil_SetTlbAttributes((UINTPTR)descriptors,NORM_NONCACHE);
 if(XEmacPs_BdRingCreate(&mac.RxBdRing,(UINTPTR)descriptors,(UINTPTR)descriptors,XEMACPS_BD_ALIGNMENT,RX_N)!=XST_SUCCESS)return ERR_IF;
 if(XEmacPs_BdRingCreate(&mac.TxBdRing,(UINTPTR)(descriptors+4096),(UINTPTR)(descriptors+4096),XEMACPS_BD_ALIGNMENT,TX_N)!=XST_SUCCESS)return ERR_IF;
 XEmacPs_BdClear(&template);if(XEmacPs_BdRingClone(&mac.RxBdRing,&template,XEMACPS_RECV)!=XST_SUCCESS)return ERR_IF;
 XEmacPs_BdSetTxUsed(&template);if(XEmacPs_BdRingClone(&mac.TxBdRing,&template,XEMACPS_SEND)!=XST_SUCCESS)return ERR_IF;
 if(XEmacPs_BdRingAlloc(&mac.RxBdRing,RX_N,&first)!=XST_SUCCESS)return ERR_IF;
 for(i=0,bd=first;i<RX_N;++i,bd=XEmacPs_BdRingNext(&mac.RxBdRing,bd)){
  Xil_DCacheInvalidateRange((INTPTR)rxbuf[i],BUF);XEmacPs_BdSetAddressRx(bd,(UINTPTR)rxbuf[i]);XEmacPs_BdClearRxNew(bd);
 }
 if(XEmacPs_BdRingToHw(&mac.RxBdRing,RX_N,first)!=XST_SUCCESS)return ERR_IF;
 XEmacPs_SetQueuePtr(&mac,(UINTPTR)mac.RxBdRing.BaseBdAddr,0,XEMACPS_RECV);
 XEmacPs_SetQueuePtr(&mac,(UINTPTR)mac.TxBdRing.BaseBdAddr,0,XEMACPS_SEND);
 XEmacPs_IntDisable(&mac,XEMACPS_IXR_ALL_MASK);
 phy=32;scan=configured_speed=0;n->name[0]='e';n->name[1]='0';n->mtu=1500;
 n->output=etharp_output;n->linkoutput=output;n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP;
 return ERR_OK;
}
void gem_poll_link(struct netif*n){
 u16 a,b,local,remote;unsigned speed;u32 clk;
 if(phy>=32){
  if(XEmacPs_PhyRead(&mac,scan,2,&a)==XST_SUCCESS&&a&&a!=0xffff){phy=scan;(void)XEmacPs_PhyRead(&mac,phy,0,&a);(void)XEmacPs_PhyWrite(&mac,phy,0,(u16)((a&~0x0800u)|0x1200u));}
  scan=(scan+1u)%32u;return;
 }
 if(XEmacPs_PhyRead(&mac,phy,1,&a)!=XST_SUCCESS||XEmacPs_PhyRead(&mac,phy,1,&b)!=XST_SUCCESS||!(b&4u)||!(b&0x20u)){
  netif_set_link_down(n);return;
 }
 if(XEmacPs_PhyRead(&mac,phy,9,&local)!=XST_SUCCESS||XEmacPs_PhyRead(&mac,phy,10,&remote)!=XST_SUCCESS)return;
 speed=((local&0x200u)&&(remote&0x800u))?1000u:0u;
 if(!speed){if(XEmacPs_PhyRead(&mac,phy,4,&local)!=XST_SUCCESS||XEmacPs_PhyRead(&mac,phy,5,&remote)!=XST_SUCCESS)return;
  if(local&remote&0x100u)speed=100;else if(local&remote&0x40u)speed=10;else return;
 }
 if(speed!=configured_speed){
  XEmacPs_Stop(&mac);Xil_Out32(0xf8000008u,0xdf0du);clk=Xil_In32(0xf8000140u)&0xfc0fc0ffu;
  clk|=8u<<8;clk|=(speed==1000u?1u:speed==100u?5u:50u)<<20;
  Xil_Out32(0xf8000140u,clk);Xil_Out32(0xf8000004u,0x767bu);
  XEmacPs_SetOperatingSpeed(&mac,(u16)speed);XEmacPs_Start(&mac);XEmacPs_IntDisable(&mac,XEMACPS_IXR_ALL_MASK);configured_speed=speed;
 }
 netif_set_link_up(n);
}
void gem_poll_input(struct netif*n,unsigned budget){
 XEmacPs_Bd *first,*bd;unsigned count,i;
 tx_reclaim();count=XEmacPs_BdRingFromHwRx(&mac.RxBdRing,budget,&first);
 for(i=0,bd=first;i<count;++i,bd=XEmacPs_BdRingNext(&mac.RxBdRing,bd)){
  unsigned len=XEmacPs_BdGetLength(bd),index=((UINTPTR)bd-mac.RxBdRing.BaseBdAddr)/mac.RxBdRing.Separation;
  if(index<RX_N&&len>=14&&len<=1514&&XEmacPs_BdIsRxSOF(bd)&&XEmacPs_BdIsRxEOF(bd)){
   struct pbuf*p;Xil_DCacheInvalidateRange((INTPTR)rxbuf[index],BUF);p=pbuf_alloc(PBUF_RAW,(u16_t)len,PBUF_POOL);
   if(p){if(pbuf_take(p,rxbuf[index],(u16_t)len)!=ERR_OK||n->input(p,n)!=ERR_OK)pbuf_free(p);}
  }
 }
 if(count){
  (void)XEmacPs_BdRingFree(&mac.RxBdRing,count,first);
  if(XEmacPs_BdRingAlloc(&mac.RxBdRing,count,&first)!=XST_SUCCESS)return;
  for(i=0,bd=first;i<count;++i,bd=XEmacPs_BdRingNext(&mac.RxBdRing,bd)){XEmacPs_BdSetStatus(bd,0);XEmacPs_BdClearRxNew(bd);}
  dsb();(void)XEmacPs_BdRingToHw(&mac.RxBdRing,count,first);
 }
 XEmacPs_WriteReg(mac.Config.BaseAddress,XEMACPS_RXSR_OFFSET,0xffffffffu);
 XEmacPs_WriteReg(mac.Config.BaseAddress,XEMACPS_TXSR_OFFSET,0xffffffffu);
}
