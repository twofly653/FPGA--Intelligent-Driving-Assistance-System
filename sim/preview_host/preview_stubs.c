#include "preview_stubs.h"
#include "lcd_gpio.h"
#include "pl_hub.h"
#include <string.h>
XTime preview_ticks=1000000ULL;
environment_snapshot_t preview_env;
attitude_est_snapshot_t preview_pose;
collision_model_t preview_collision;
obstacle_service_snapshot_t preview_obstacle;
obstacle_display_snapshot_t preview_display;
int preview_sensor_ready=1,preview_mic=46;
static unsigned preview_enabled=1,preview_ws=0;
void preview_defaults(void){
 memset(&preview_env,0,sizeof preview_env);memset(&preview_pose,0,sizeof preview_pose);
 preview_env.temperature_x10=246;preview_env.humidity_x10=582;preview_env.pressure_hpa=1013;
 preview_env.light=(veml_sample_t){1217,834,1094,691,82,420};
 preview_env.environment_fresh=preview_env.pressure_fresh=preview_env.light_fresh=1;
 preview_env.hdc_detected=preview_env.spa_detected=preview_env.light_detected=1;
 preview_pose.imu_detected=preview_pose.mag_detected=preview_pose.ready=preview_pose.imu_fresh=1;
 preview_pose.mag_fresh=preview_pose.roll_pitch_recent=1;preview_pose.roll_deg=12.4f;
 preview_pose.pitch_deg=-3.7f;preview_pose.yaw_deg=42.6f;preview_pose.heading_deg=42.6f;
 preview_collision=(collision_model_t){COLLISION_SAFE,COLLISION_SAFE,0,1,1234,0};
 preview_sensor_ready=1;preview_enabled=1;
 preview_obstacle=(obstacle_service_snapshot_t){17,432,16,8,431,400,0,1,1,0,3};
 memset(&preview_display,0,sizeof preview_display);
 preview_display.sequence=17;preview_display.source_frame=432;preview_display.age_ms=16;
 preview_display.available=1;preview_display.status=0;preview_display.count=3;
}
void lcd_gpio_init(void){}
void lcd_gpio_set(u32 m,u32 v){(void)m;(void)v;}
void lcd_spi_write_byte(u8 d){(void)d;}
void lcd_spi_burst_u16(const u16 *d,u32 n){(void)d;(void)n;}
void i2c_init(void){}
u32 i2c_gpio_read(void){return 3;}
int i2c_probe(u8 a){(void)a;return 0;}
int vl53l0x_probe(void){return 0;}
int cst816_init(void){return 0;}
void environment_service_init(void){}
void environment_service_get_snapshot(environment_snapshot_t *o){*o=preview_env;}
void attitude_est_get_snapshot(attitude_est_snapshot_t *o){*o=preview_pose;}
void attitude_est_request_realign(void){preview_pose.aligning=1;preview_pose.ready=0;preview_pose.warmup_elapsed_s=.65f;}
int attitude_est_cached_imu(lsm6_sample_t *s,unsigned *n){
 s->acc_g[0]=.012f;s->acc_g[1]=-.064f;s->acc_g[2]=.998f;
 s->gyro_dps[0]=.2f;s->gyro_dps[1]=-.4f;s->gyro_dps[2]=.1f;if(n)*n=43;return preview_pose.imu_fresh;
}
const collision_model_t *collision_service_state(void){return &preview_collision;}
int collision_service_sensor_ready(void){return preview_sensor_ready;}
unsigned obstacle_service_is_enabled(void){return preview_enabled;}
void obstacle_service_set_enabled(unsigned e){preview_enabled=e?1:0;preview_obstacle.enabled=preview_enabled;}
void obstacle_service_get_snapshot(obstacle_service_snapshot_t *o){*o=preview_obstacle;}
void obstacle_service_get_display_snapshot(obstacle_display_snapshot_t *o){*o=preview_display;}
u32 pl_status(void){return preview_ws?ST_WS_BUSY:0;}
void ws_start(u32 mode){preview_ws=mode;}
void ws_set_bright(u8 b){(void)b;}
u32 ws_last_mode(void){return preview_ws;}
u8 mic_db(void){return (u8)preview_mic;}
