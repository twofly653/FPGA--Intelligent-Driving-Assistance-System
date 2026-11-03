#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "xtime_l.h"
#include "vl53l0x.h"
#include "vl53l0x_reset.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform_status.h"
#ifndef APP_TOF_SOFT_RESET
#define APP_TOF_SOFT_RESET 0
#endif

enum fault { OK, ASSERT_STUCK, BOOT_STUCK, ASSERT_WRITE_NACK,
    RELEASE_WRITE_NACK, RELEASE_ONCE_NACK, ASSERT_READ_NACK, BOOT_READ_NACK,
    ASSERT_TRANSIENT_NACK, BOOT_TRANSIENT_NACK, BOTH_TRANSIENT_NACK,
    ASSERT_API_ERROR, BOOT_API_ERROR, WRONG_CHIP, WRONG_BOOT_ID,
    INITIAL_NACK, INITIAL_STICKY, LATE_BOOT };
static enum fault fault;
static uint64_t ticks, phase_started, assert_duration, boot_duration;
static unsigned phase, assert_reads, boot_reads, release_writes, assert_writes;
static unsigned data_calls, start_calls, config_calls, read_latency, frozen_clock;
static unsigned sample_status, sample_mm, sample_reads, sample_ready, cases;
static unsigned fail_stage, sticky_stage, last_api_stage;
static int io_error;

static void advance(unsigned us) { if (!frozen_clock) ticks += us; }
void XTime_GetTime(XTime *out) { *out=ticks; }
int usleep(unsigned us) { assert(us<=2000u); advance(us); return 0; }
void vl53l0x_platform_clear_io_error(void) { io_error=0; }
int vl53l0x_platform_io_error(void) { return io_error; }
static VL53L0X_Error nack(void) { io_error=1; return VL53L0X_ERROR_CONTROL_INTERFACE; }
VL53L0X_Error VL53L0X_WrByte(VL53L0X_DEV d,uint8_t reg,uint8_t value)
{
    assert(d && d->I2cDevAddr==0x29u && reg==0xBFu);
    if (!value) {
        ++assert_writes;phase=1;phase_started=ticks;
        if(fault==ASSERT_WRITE_NACK)return nack();
    } else {
        assert(value==1u);++release_writes;
        if(phase==1)assert_duration=ticks-phase_started;
        else if(phase==2)boot_duration=ticks-phase_started;
        phase=2;phase_started=ticks;
        if(fault==RELEASE_WRITE_NACK ||
           (fault==RELEASE_ONCE_NACK && release_writes==1))return nack();
    }
    return 0;
}
VL53L0X_Error VL53L0X_RdByte(VL53L0X_DEV d,uint8_t reg,uint8_t *value)
{
    assert(d && reg==0xC0u && phase==1);++assert_reads;advance(read_latency);
    assert_duration=ticks-phase_started;
    if(fault==ASSERT_READ_NACK ||
       ((fault==ASSERT_TRANSIENT_NACK || fault==BOTH_TRANSIENT_NACK) && assert_reads<=2))return nack();
    if(fault==ASSERT_API_ERROR)return (VL53L0X_Error)-4;
    *value=fault==ASSERT_STUCK?0xEEu:0u;return 0;
}
VL53L0X_Error VL53L0X_RdWord(VL53L0X_DEV d,uint8_t reg,uint16_t *value)
{
    assert(d && d->I2cDevAddr==0x29u && reg==0xC0u);
    if(!phase) {
        if(fault==INITIAL_NACK)return nack();
        if(fault==INITIAL_STICKY)io_error=1;
        *value=fault==WRONG_CHIP?0x1234u:0xEEAAu;return 0;
    }
    assert(phase==2);++boot_reads;advance(fault==LATE_BOOT?100000u:read_latency);
    boot_duration=ticks-phase_started;
    if(fault==BOOT_READ_NACK ||
       ((fault==BOOT_TRANSIENT_NACK || fault==BOTH_TRANSIENT_NACK) && boot_reads<=2))return nack();
    if(fault==BOOT_API_ERROR)return (VL53L0X_Error)-4;
    *value=fault==BOOT_STUCK?0u:(fault==WRONG_BOOT_ID?0xEEABu:0xEEAAu);return 0;
}
static VL53L0X_Error api(unsigned stage)
{
    assert(stage==last_api_stage+1u);last_api_stage=stage;
    if(sticky_stage==stage)io_error=1;
    return fail_stage==stage?(VL53L0X_Error)-4:0;
}
VL53L0X_Error VL53L0X_StopMeasurement(VL53L0X_DEV d) { (void)d;return 0; }
VL53L0X_Error VL53L0X_DataInit(VL53L0X_DEV d)
{
    (void)d;
    if(APP_TOF_SOFT_RESET)assert(phase==2 && boot_reads>0);
    else assert(phase==0 && assert_writes==0 && release_writes==0);
    assert(io_error==0);++data_calls;return api(3);
}
VL53L0X_Error VL53L0X_StaticInit(VL53L0X_DEV d) { (void)d;++config_calls;return api(4); }
VL53L0X_Error VL53L0X_PerformRefCalibration(VL53L0X_DEV d,uint8_t *v,uint8_t *p)
{ (void)d;*v=1;*p=2;++config_calls;return api(5); }
VL53L0X_Error VL53L0X_PerformRefSpadManagement(VL53L0X_DEV d,uint32_t *n,uint8_t *a)
{ (void)d;*n=10;*a=1;++config_calls;return api(6); }
VL53L0X_Error VL53L0X_SetLimitCheckEnable(VL53L0X_DEV d,uint16_t id,uint8_t on)
{
    (void)d;assert(on==1);++config_calls;
    assert(id==VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE || id==VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE);
    return api(id==VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE?7:8);
}
VL53L0X_Error VL53L0X_SetLimitCheckValue(VL53L0X_DEV d,uint16_t id,FixPoint1616_t v)
{
    (void)d;++config_calls;
    if(id==VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE){assert(v==(60u<<16));return api(10);}
    assert(id==VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE && v==6554u);return api(9);
}
VL53L0X_Error VL53L0X_SetMeasurementTimingBudgetMicroSeconds(VL53L0X_DEV d,uint32_t v)
{ (void)d;assert(v==33000u);++config_calls;return api(11); }
VL53L0X_Error VL53L0X_SetVcselPulsePeriod(VL53L0X_DEV d,VL53L0X_VcselPeriod type,uint8_t v)
{
    (void)d;++config_calls;
    if(type==VL53L0X_VCSEL_PERIOD_PRE_RANGE){assert(v==18);return api(12);}
    assert(type==VL53L0X_VCSEL_PERIOD_FINAL_RANGE && v==14);return api(13);
}
VL53L0X_Error VL53L0X_SetDeviceMode(VL53L0X_DEV d,VL53L0X_DeviceModes mode)
{ (void)d;assert(mode==VL53L0X_DEVICEMODE_CONTINUOUS_RANGING);++config_calls;return api(14); }
VL53L0X_Error VL53L0X_StartMeasurement(VL53L0X_DEV d)
{ (void)d;assert(data_calls==1 && config_calls==11);++start_calls;return api(15); }
VL53L0X_Error VL53L0X_GetMeasurementDataReady(VL53L0X_DEV d,uint8_t *ready)
{ (void)d;++sample_reads;*ready=(uint8_t)sample_ready;return 0; }
VL53L0X_Error VL53L0X_GetRangingMeasurementData(VL53L0X_DEV d,VL53L0X_RangingMeasurementData_t *s)
{ (void)d;memset(s,0,sizeof *s);s->RangeStatus=(uint8_t)sample_status;s->RangeMilliMeter=(uint16_t)sample_mm;return 0; }
VL53L0X_Error VL53L0X_ClearInterruptMask(VL53L0X_DEV d,uint32_t mask)
{ (void)d;assert(mask==0);return 0; }
static void setup(enum fault f,unsigned latency,unsigned frozen)
{
    fault=f;ticks=phase_started=assert_duration=boot_duration=0;
    phase=assert_reads=boot_reads=release_writes=assert_writes=0;
    data_calls=start_calls=config_calls=sample_reads=0;
    read_latency=latency;frozen_clock=frozen;io_error=0;
    sample_status=0;sample_mm=260;sample_ready=1;
    fail_stage=sticky_stage=0;last_api_stage=2;
}
static void verify(int expected,unsigned stage)
{
    const vl53l0x_init_diagnostic_t *d=vl53l0x_init_diagnostic();int mm=777;
    assert(d->result==expected && d->stage==stage);
    assert(d->reset_enabled==APP_TOF_SOFT_RESET);
    assert(assert_reads<=VL53L0X_RESET_MAX_READS && boot_reads<=VL53L0X_RESET_MAX_READS);
    assert(assert_duration<=VL53L0X_RESET_TIMEOUT_US && boot_duration<=VL53L0X_RESET_TIMEOUT_US);
    if(expected) {
        assert(vl53l0x_read_mm(&mm)==-1 && mm==777 && sample_reads==0);
        if(stage<=2)assert(data_calls==0 && start_calls==0 && config_calls==0);
        if(stage>=3)assert(last_api_stage==stage);
    } else {
        unsigned s;
        assert(data_calls==1 && start_calls==1 && last_api_stage==15 && io_error==0);
        assert(vl53l0x_read_mm(&mm)==0 && mm==260);
        assert(vl53l0x_read_mm(NULL)==-1);
        sample_ready=0;mm=777;assert(vl53l0x_read_mm(&mm)==-2 && mm==777);sample_ready=1;
        for(s=1;s<=5;++s){sample_status=s;sample_mm=s==4?8190u:8191u;mm=777;assert(vl53l0x_read_mm(&mm)==-3 && mm==777);}
        if(APP_TOF_SOFT_RESET)assert(assert_writes==1 && release_writes==1);
        else assert(assert_writes==0 && release_writes==0);
    }
}
static void run(const char *name,enum fault f,int expected,unsigned stage,unsigned latency,unsigned frozen)
{
    const vl53l0x_reset_diagnostic_t *d;int rc;
    setup(f,latency,frozen);rc=vl53l0x_init();assert(rc==expected);verify(expected,stage);
    d=vl53l0x_reset_diagnostic();
    if(APP_TOF_SOFT_RESET && stage==2){
        assert(d->result==expected && release_writes>=1 && release_writes<=2);
        assert(d->stage==(f==ASSERT_WRITE_NACK?1u:(f==RELEASE_WRITE_NACK||f==RELEASE_ONCE_NACK?3u:(f==ASSERT_STUCK||f==ASSERT_READ_NACK||f==ASSERT_API_ERROR||latency==100000u?2u:4u))));
    }
    if(APP_TOF_SOFT_RESET && (f==ASSERT_TRANSIENT_NACK||f==BOOT_TRANSIENT_NACK||f==BOTH_TRANSIENT_NACK)){
        unsigned n=f==BOTH_TRANSIENT_NACK?4u:2u;
        assert(d->stage==5 && d->read_nacks==n && d->first_api_status==VL53L0X_ERROR_CONTROL_INTERFACE);
        assert(vl53l0x_init_diagnostic()->reset_io_seen==1 && vl53l0x_init_diagnostic()->reset_read_nacks==n);
    }
    if(APP_TOF_SOFT_RESET && frozen && f==ASSERT_STUCK)assert(assert_reads==50);
    if(APP_TOF_SOFT_RESET && frozen && f==BOOT_STUCK)assert(boot_reads==50);
    ++cases;printf("PASS %s: rc=%d stage=%u reads=%u/%u\n",name,rc,stage,assert_reads,boot_reads);
}
int main(void)
{
    unsigned stage;VL53L0X_Dev_t dev;
    run("normal original sequence",OK,0,16,1000,0);
    run("wrong initial chip",WRONG_CHIP,-1,1,1000,0);
    run("initial read NACK",INITIAL_NACK,-1,1,1000,0);
    assert(vl53l0x_init_diagnostic()->api_status==VL53L0X_ERROR_CONTROL_INTERFACE);
    run("initial sticky I/O with success return",INITIAL_STICKY,-1,1,1000,0);
    assert(vl53l0x_init_diagnostic()->api_status==0 && vl53l0x_init_diagnostic()->io_error==1);
    for(stage=3;stage<=15;++stage){
        int rc;setup(OK,1000,0);fail_stage=stage;rc=vl53l0x_init();assert(rc==-4);verify(-4,stage);
        assert(vl53l0x_init_diagnostic()->api_status==-4);++cases;
        setup(OK,1000,0);sticky_stage=stage;rc=vl53l0x_init();assert(rc==-4);verify(-4,stage);
        assert(vl53l0x_init_diagnostic()->api_status==0 && vl53l0x_init_diagnostic()->io_error==1);++cases;
    }
    printf("PASS all 13 ST stages: API and sticky-only failures stop immediately\n");
    if(APP_TOF_SOFT_RESET){
        run("never enters reset",ASSERT_STUCK,-6,2,1000,0);
        run("never boots",BOOT_STUCK,-7,2,1000,0);
        run("assert write NACK",ASSERT_WRITE_NACK,-5,2,1000,0);
        run("release write NACK",RELEASE_WRITE_NACK,-5,2,1000,0);
        run("release NACK remains fatal after cleanup success",RELEASE_ONCE_NACK,-5,2,1000,0);
        run("persistent assert NACK bounded timeout",ASSERT_READ_NACK,-6,2,1000,0);
        run("persistent boot NACK bounded timeout",BOOT_READ_NACK,-7,2,1000,0);
        run("assert transient NACK recovers",ASSERT_TRANSIENT_NACK,0,16,1000,0);
        run("boot transient NACK recovers",BOOT_TRANSIENT_NACK,0,16,1000,0);
        run("both transient NACKs recover",BOTH_TRANSIENT_NACK,0,16,1000,0);
        run("assert non-control API error",ASSERT_API_ERROR,-5,2,1000,0);
        run("boot non-control API error",BOOT_API_ERROR,-5,2,1000,0);
        run("wrong reboot full identity",WRONG_BOOT_ID,-7,2,1000,0);
        run("assert slow I/O bounded",ASSERT_STUCK,-6,2,4000,0);
        run("boot slow I/O bounded",BOOT_STUCK,-7,2,4000,0);
        run("assert stopped clock read cap",ASSERT_STUCK,-6,2,0,1);
        run("boot stopped clock read cap",BOOT_STUCK,-7,2,0,1);
        run("valid response before deadline",OK,0,16,94999,0);
        run("valid assert response at deadline rejected",OK,-6,2,100000,0);
        run("valid boot response at deadline rejected",LATE_BOOT,-7,2,1000,0);
    } else {
        run("default ignores reset-only read fault",ASSERT_READ_NACK,0,16,1000,0);
        run("default ignores reset-only write fault",ASSERT_WRITE_NACK,0,16,1000,0);
    }
    run("success after prior failures",OK,0,16,1000,0);
    setup(ASSERT_TRANSIENT_NACK,1000,0);memset(&dev,0,sizeof dev);dev.I2cDevAddr=0x29u;
    assert(vl53l0x_reset_bounded(&dev)==0 && io_error==1);
    assert(vl53l0x_reset_diagnostic()->read_nacks==2);++cases;
    assert(vl53l0x_reset_bounded(NULL)==-5);
    assert(vl53l0x_reset_diagnostic()->stage==0 && vl53l0x_reset_diagnostic()->release_attempts==0);++cases;
    printf("%u ToF fault/integration cases passed; reset=%d\n",cases,APP_TOF_SOFT_RESET);return 0;
}
