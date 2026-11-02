#ifndef PC_NET_PROTOCOL_H
#define PC_NET_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define PN_HEADER 48u
#define PN_CHUNK 1344u
#define PN_MAX_FRAME (640u*480u*2u)
#define PN_MAX_BOXES 8u
enum {PN_HELLO=1,PN_FRAME=2,PN_RESULT=3,PN_STATUS=4};
typedef struct {
 uint8_t type; uint16_t flags;
 uint32_t session,epoch,frame; uint64_t capture_us;
 uint16_t width,height,index,chunks; uint32_t total;
 uint16_t length;
} pn_header_t;
typedef struct {uint32_t id;uint16_t x1,y1,x2,y2,score;uint8_t cls,flags;} pn_box_t;
uint32_t pn_crc32(const uint8_t *p,size_t n,uint32_t crc);
size_t pn_pack(uint8_t *out,size_t capacity,const pn_header_t *h,const void *payload);
int pn_unpack(const uint8_t *p,size_t n,pn_header_t *h);
size_t pn_result_pack(uint8_t *out,size_t cap,uint16_t proc_ms,const pn_box_t *boxes,unsigned n);
int pn_result_read(const uint8_t *p,size_t n,unsigned width,unsigned height,pn_box_t *boxes,unsigned *count,uint16_t *proc_ms);
#endif
