#include <stdio.h>
#include <stdlib.h>
#include "lcd_st7789.h"
#include "ui_home.h"
#include "ui_menu.h"
#include "ui_compass.h"
#include "ui_attitude.h"
#include "ui_distance.h"
#include "ui_obstacle.h"
#include "ui_clock.h"
#include "touch_hit.h"
#include "preview_stubs.h"
extern unsigned ui_host_text_overflows;
static ui_page_t page;
static void save(const char *name){
 char path[256];snprintf(path,sizeof path,"frames/%s.ppm",name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(2);}fprintf(f,"P6\n%d %d\n255\n",LCD_W,LCD_H);
 const u16 *fb=lcd_host_framebuffer();
 for(unsigned i=0;i<LCD_W*LCD_H;++i){unsigned v=fb[i];unsigned char rgb[3]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};fwrite(rgb,1,3,f);}
 fclose(f);printf("FRAME %s overflows=%u\n",name,ui_host_text_overflows);
}
static void enter(int item){ui_menu_enter(&page);touch_hit_set_pending_sel(item);ui_menu_on_input(IN_TAP,&page);}
static void poll(void){preview_ticks+=300000;ui_menu_poll(page);}
int main(void){
 preview_defaults();lcd_init();ui_sensors_init();ui_clock_command("TIME SET 2026-10-28T14:38:26");
 ui_home_draw_static();ui_home_draw_clock(0,12,34);ui_home_draw_timer_state(SW_RUN);save("01-home-running");
 ui_home_draw_timer_state(SW_PAUSE);save("02-home-paused");
 ui_clock_reset();ui_home_draw_static();ui_home_draw_clock(0,0,0);ui_home_draw_timer_state(SW_IDLE);save("03-home-await-sync");
 ui_menu_enter(&page);save("04-menu-1");ui_menu_on_input(IN_E,&page);save("05-menu-2");ui_menu_on_input(IN_E,&page);save("06-menu-3");
 enter(1);save("07-cabin");preview_env.temperature_x10=1250;preview_env.humidity_x10=1000;poll();save("08-cabin-extreme");
 preview_env.environment_fresh=preview_env.pressure_fresh=0;poll();save("09-cabin-offline");preview_defaults();
 enter(3);save("10-light-lux");touch_hit_set_pending_sel(1);ui_menu_on_input(IN_TAP,&page);save("11-light-channels");
 preview_env.light_fresh=0;poll();save("12-light-offline");preview_defaults();
 enter(5);save("13-imu");preview_pose.imu_fresh=0;poll();save("14-imu-offline");preview_defaults();
 enter(6);poll();save("15-sound-off");touch_hit_set_pending_sel(1);ui_menu_on_input(IN_TAP,&page);save("16-sound-on");
 enter(7);save("17-ambient-off");ui_menu_on_input(IN_C,&page);save("18-ambient-on");
 ui_compass_enter();save("19-heading");ui_compass_on_input(IN_E);save("20-heading-info");
 preview_pose.ready=preview_pose.imu_fresh=0;ui_compass_enter();save("21-heading-offline");preview_defaults();
 ui_attitude_enter();save("22-attitude");ui_attitude_on_input(IN_E);save("23-attitude-aligning");
 preview_pose.imu_detected=0;preview_ticks+=300000;ui_attitude_poll();save("24-attitude-offline");preview_defaults();
 ui_distance_enter();save("25-distance-safe");preview_collision.distance_mm=650;preview_collision.level=COLLISION_WARNING;ui_distance_poll();save("26-distance-warning");
 preview_collision.distance_mm=180;preview_collision.level=COLLISION_DANGER;ui_distance_poll();save("27-distance-danger");
 preview_sensor_ready=0;ui_distance_poll();save("28-distance-offline");preview_defaults();
 ui_obstacle_enter();save("29-objects-on");touch_hit_set_pending_sel(2);ui_obstacle_on_input(IN_TAP);save("30-objects-off");
 obstacle_service_set_enabled(1);preview_obstacle.status=1;ui_obstacle_enter();save("31-objects-warmup");
 preview_obstacle.status=2;ui_obstacle_enter();save("32-objects-motion");
 preview_obstacle.available=0;ui_obstacle_enter();save("33-objects-offline");
 printf("TOTAL_TEXT_OVERFLOWS=%u\n",ui_host_text_overflows);return ui_host_text_overflows?1:0;
}
