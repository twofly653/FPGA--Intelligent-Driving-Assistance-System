#include "pc_net_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(v) do{assert(v);++checks;}while(0)
static void golden(const char*name,const uint8_t*p,size_t n){size_t i;printf("%s=",name);for(i=0;i<n;++i)printf("%02x",p[i]);puts("");}
int main(void){
 uint8_t packet[1400],bad[1400],body[150];size_t n,j;unsigned count;uint16_t proc;
 pn_header_t h={0},decoded;pn_box_t box[8]={{0}},got[8];pn_guard_t guard;
 CHECK(pn_crc32((const uint8_t*)"123456789",9,0)==0xcbf43926u);
 h.type=PN_HELLO;h.session=0x12345678;h.flags=1;n=pn_pack(packet,sizeof packet,&h,0);CHECK(n==48&&pn_unpack(packet,n,&decoded));golden("HELLO",packet,n);
 h.type=PN_FRAME;h.frame=9;h.epoch=4;h.capture_us=123456;h.width=2;h.height=2;h.chunks=1;h.total=h.length=8;
 for(j=0;j<8;++j)body[j]=(uint8_t)j;
 n=pn_pack(packet,sizeof packet,&h,body);CHECK(pn_unpack(packet,n,&decoded));golden("FRAME",packet,n);
 for(j=0;j<n;++j){memcpy(bad,packet,n);bad[j]^=1;CHECK(!pn_unpack(bad,n,&decoded));}
 for(j=0;j<n;++j)CHECK(!pn_unpack(packet,j,&decoded));
 h.width=641;h.total=641u*2u*2u;CHECK(pn_pack(packet,sizeof packet,&h,body)&&!pn_unpack(packet,n,&decoded));
 box[0]=(pn_box_t){17,10,20,29,39,901,2,1};n=pn_result_pack(body,sizeof body,27,box,1);
 CHECK(pn_result_read(body,n,320,240,got,&count,&proc)&&count==1&&proc==27&&got[0].id==17);
 h=(pn_header_t){0};h.type=PN_RESULT;h.session=0x12345678;h.flags=1;h.epoch=4;h.frame=9;h.capture_us=123456;h.width=320;h.height=240;h.length=(uint16_t)n;
 n=pn_pack(packet,sizeof packet,&h,body);golden("RESULT",packet,n);CHECK(pn_unpack(packet,n,&decoded));
 box[1]=box[0];j=pn_result_pack(body,sizeof body,0,box,2);CHECK(!pn_result_read(body,j,320,240,got,&count,&proc));
 box[0].x2=320;j=pn_result_pack(body,sizeof body,0,box,1);CHECK(!pn_result_read(body,j,320,240,got,&count,&proc));
 box[0]=(pn_box_t){17,10,20,29,39,1001,2,1};j=pn_result_pack(body,sizeof body,0,box,1);CHECK(!pn_result_read(body,j,320,240,got,&count,&proc));
 pn_guard_reset(&guard,h.session,4,1);CHECK(!pn_guard_accept(&guard,&h,200000));
 pn_guard_issue(&guard,9,123456);CHECK(pn_guard_accept(&guard,&h,200000));CHECK(!pn_guard_accept(&guard,&h,210000));
 h.frame=10;h.capture_us=223456;pn_guard_issue(&guard,10,223456);
 h.session++;CHECK(!pn_guard_accept(&guard,&h,300000));h.session--;h.epoch++;CHECK(!pn_guard_accept(&guard,&h,300000));h.epoch--;
 CHECK(!pn_guard_accept(&guard,&h,700000));CHECK(!pn_guard_accept(&guard,&h,200000));
 guard.enabled=0;CHECK(!pn_guard_accept(&guard,&h,300000));guard.enabled=1;h.flags=0;CHECK(!pn_guard_accept(&guard,&h,300000));h.flags=1;
 CHECK(pn_guard_accept(&guard,&h,300000));
 for(j=0;j<12;++j)pn_guard_issue(&guard,(uint32_t)(20+j),300000+j);
 h.frame=20;h.capture_us=300000;CHECK(!pn_guard_accept(&guard,&h,400000));
 printf("CHECKS=%u\n",checks);return 0;
}
