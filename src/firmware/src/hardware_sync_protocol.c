#include "hardware_sync_protocol.h"
#include <stdio.h>
int hardware_sync_next_due(unsigned mask,unsigned *cursor){
 unsigned k;if(!cursor)return -1;
 for(k=0;k<7u;++k){unsigned id=(*cursor+k)%7u;if(mask&(1u<<id)){*cursor=(id+1u)%7u;return (int)id;}}
 return -1;
}
int hardware_sync_detection(char *out,size_t size,uint32_t sequence,unsigned enabled){
 int n;if(!out||!size)return -1;out[0]=0;if(enabled>1u)return -1;
 n=snprintf(out,size,"DETECT %u %u\r\n",(unsigned)sequence,enabled);
 if(n<0||(size_t)n>=size){out[0]=0;return -1;}return n;
}
int hardware_sync_camera(char *out,size_t size,const camera_sync_snapshot_t *v){
 int n;
 if(!out||!size||!v)return -1;
 out[0]=0;
 if(v->mode>3u||v->live>1u||v->configured>1u||v->width!=640u||v->height!=480u||(v->live&&!v->configured))return -1;
 n=snprintf(out,size,"CAMERA %u %u %u %u %u %u\r\n",(unsigned)v->sequence,v->mode,v->live,v->configured,v->width,v->height);
 if(n<0||(size_t)n>=size){out[0]=0;return -1;}return n;
}
int hardware_sync_obstacles(char *out,size_t size,const obstacle_display_snapshot_t *v){
 int n;size_t used;unsigned i,j;
 if(!out||!size||!v)return -1;
 out[0]=0;
 if(v->available>1u||v->status>5u||v->count>8u||v->age_ms>0x7fffffffu||v->proc_ms>0x7fffffffu||
   (v->count&&(!v->available||v->status!=0u||v->age_ms>500u)))return -1;
 for(i=0;i<v->count;++i){const obstacle_display_box_t *b=&v->boxes[i];
  if(!b->id||b->xmin>b->xmax||b->ymin>b->ymax||b->xmax>=640u||b->ymax>=480u)return -1;
  for(j=0;j<i;++j)if(v->boxes[j].id==b->id)return -1;
 }
 n=snprintf(out,size,"OBSTACLE %u %u %u %u %u %u %u",(unsigned)v->sequence,(unsigned)v->source_frame,v->status,v->available,(unsigned)v->age_ms,(unsigned)v->proc_ms,v->count);
 if(n<0||(size_t)n>=size)goto fail;
 used=(size_t)n;
 for(i=0;i<v->count;++i){const obstacle_display_box_t *b=&v->boxes[i];
  n=snprintf(out+used,size-used," %u %u %u %u %u",b->id,b->xmin,b->ymin,b->xmax,b->ymax);
  if(n<0||(size_t)n>=size-used)goto fail;
  used+=(size_t)n;
 }
 if(used+3u>size)goto fail;
 out[used++]='\r';out[used++]='\n';out[used]=0;return (int)used;
fail:out[0]=0;return -1;
}
