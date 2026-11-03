/* Offline immutable RGB565 sequence replay; never board I/O. */
#include "obstacle_motion_core.h"
#include <stdio.h>
#include <stdint.h>
static obstacle_motion_t state;
static uint16_t pixels[320u*240u];
int main(int argc,char **argv){FILE *in,*out;unsigned frame=0;size_t n;
 if(argc!=3){fprintf(stderr,"usage: obstacle_replay input.rgb565_stream output.json\n");return 2;}
 in=fopen(argv[1],"rb");out=fopen(argv[2],"wb");if(!in||!out)return 2;
 obstacle_motion_init(&state);fputs("[\n",out);
 while((n=fread(pixels,1,sizeof pixels,in))!=0){unsigned i;obstacle_motion_result_t result;
  if(n!=sizeof pixels){fclose(in);fclose(out);return 3;}
  obstacle_motion_process(&state,pixels,320,240,frame+1u,1000u+frame*200u,1000u+frame*200u,&result);
  fprintf(out,"%s{\"frame\":%u,\"status\":%u,\"count\":%u,\"objects\":[",frame?",\n":"",frame,result.status,result.count);
  for(i=0;i<result.count;++i){obstacle_motion_object_t *p=&result.objects[i];
   fprintf(out,"%s{\"id\":%u,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u,\"confidence\":%u}",i?",":"",p->id,p->bbox.x,p->bbox.y,p->bbox.w,p->bbox.h,p->confidence);
  }
  fputs("]}",out);++frame;
 }
 fputs("\n]\n",out);fclose(in);fclose(out);printf("OFFLINE_REPLAY_FRAMES=%u\n",frame);return frame?0:4;
}
