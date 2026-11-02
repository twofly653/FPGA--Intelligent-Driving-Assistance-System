#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "xtime_l.h"
#include "vl53l0x.h"
#include "vl53l0x_reset.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform_status.h"

enum fault { OK, ASSERT_STUCK, BOOT_STUCK, ASSERT_WRITE_NACK, RELEASE_WRITE_NACK,
             ASSERT_READ_NACK, BOOT_READ_NACK, WRONG_CHIP, WRONG_BOOT_ID, LATE_BOOT };
static enum fault fault;
static uint64_t ticks, phase_started, assert_duration, boot_duration;
static unsigned phase, assert_reads, boot_reads, release_writes, assert_writes;
static unsigned data_calls, start_calls, config_calls, read_latency, frozen_clock;
static unsigned sample_status, sample_mm, sample_reads, cases;
static int io_error;

static void advance(unsigned us) { if (!frozen_clock) ticks += us; }
void XTime_GetTime(XTime *out) { *out=ticks; }
int usleep(unsigned us) { assert(us <= 2000u); advance(us); return 0; }
void vl53l0x_platform_clear_io_error(void) { io_error=0; }
int vl53l0x_platform_io_error(void) { return io_error; }
static VL53L0X_Error nack(void) { io_error=1; return VL53L0X_ERROR_CONTROL_INTERFACE; }
VL53L0X_Error VL53L0X_WrByte(VL53L0X_DEV d,uint8_t reg,uint8_t value)
{
    assert(d && d->I2cDevAddr==0x29u); assert(reg==0xBFu);
    if (!value) {
        ++assert_writes; phase=1; phase_started=ticks;
        if (fault==ASSERT_WRITE_NACK) return nack();
    } else {
        assert(value==1u); ++release_writes;
        if (phase==1) assert_duration=ticks-phase_started;
        else if (phase==2) boot_duration=ticks-phase_started;
        phase=2; phase_started=ticks;
        if (fault==RELEASE_WRITE_NACK) return nack();
    }
    return VL53L0X_ERROR_NONE;
}
VL53L0X_Error VL53L0X_RdByte(VL53L0X_DEV d,uint8_t reg,uint8_t *value)
{
    assert(d && reg==0xC0u && phase==1); ++assert_reads; advance(read_latency);
    assert_duration=ticks-phase_started;
    if (fault==ASSERT_READ_NACK) return nack();
    *value=fault==ASSERT_STUCK?0xEEu:0u;
    return VL53L0X_ERROR_NONE;
}
VL53L0X_Error VL53L0X_RdWord(VL53L0X_DEV d,uint8_t reg,uint16_t *value)
{
    assert(d && d->I2cDevAddr==0x29u && reg==0xC0u);
    if (!phase) { *value=fault==WRONG_CHIP?0x1234u:0xEEAAu; return 0; }
    assert(phase==2); ++boot_reads; advance(fault==LATE_BOOT?100000u:read_latency); boot_duration=ticks-phase_started;
    if (fault==BOOT_READ_NACK) return nack();
    *value=fault==BOOT_STUCK?0u:(fault==WRONG_BOOT_ID?0xEEABu:0xEEAAu);
    return VL53L0X_ERROR_NONE;
}
VL53L0X_Error VL53L0X_StopMeasurement(VL53L0X_DEV d) {(void)d;return 0;}
VL53L0X_Error VL53L0X_DataInit(VL53L0X_DEV d)
{ (void)d; assert(phase==2 && boot_reads>0 && fault==OK); ++data_calls; return 0; }
VL53L0X_Error VL53L0X_StaticInit(VL53L0X_DEV d) {(void)d;++config_calls;return 0;}
VL53L0X_Error VL53L0X_PerformRefCalibration(VL53L0X_DEV d,uint8_t *v,uint8_t *p)
{ (void)d;*v=1;*p=2;++config_calls;return 0; }
VL53L0X_Error VL53L0X_PerformRefSpadManagement(VL53L0X_DEV d,uint32_t *n,uint8_t *a)
{ (void)d;*n=10;*a=1;++config_calls;return 0; }
VL53L0X_Error VL53L0X_SetLimitCheckEnable(VL53L0X_DEV d,uint16_t id,uint8_t on)
{ (void)d;assert(on==1);assert(id==VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE || id==VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE);++config_calls;return 0; }
VL53L0X_Error VL53L0X_SetLimitCheckValue(VL53L0X_DEV d,uint16_t id,FixPoint1616_t v)
{ (void)d;if(id==VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE)assert(v==(60u<<16));else {assert(id==VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE);assert(v==6554u);}++config_calls;return 0; }
VL53L0X_Error VL53L0X_SetMeasurementTimingBudgetMicroSeconds(VL53L0X_DEV d,uint32_t v)
{ (void)d;assert(v==33000u);++config_calls;return 0; }
VL53L0X_Error VL53L0X_SetVcselPulsePeriod(VL53L0X_DEV d,VL53L0X_VcselPeriod type,uint8_t v)
{ (void)d;assert((type==VL53L0X_VCSEL_PERIOD_PRE_RANGE && v==18)||(type==VL53L0X_VCSEL_PERIOD_FINAL_RANGE && v==14));++config_calls;return 0; }
VL53L0X_Error VL53L0X_SetDeviceMode(VL53L0X_DEV d,VL53L0X_DeviceModes mode)
{ (void)d;assert(mode==VL53L0X_DEVICEMODE_CONTINUOUS_RANGING);++config_calls;return 0; }
VL53L0X_Error VL53L0X_StartMeasurement(VL53L0X_DEV d)
{ (void)d;assert(data_calls==1 && config_calls==11);++start_calls;return 0; }
VL53L0X_Error VL53L0X_GetMeasurementDataReady(VL53L0X_DEV d,uint8_t *ready)
{ (void)d;++sample_reads;*ready=1;return 0; }
VL53L0X_Error VL53L0X_GetRangingMeasurementData(VL53L0X_DEV d,VL53L0X_RangingMeasurementData_t *s)
{ (void)d;memset(s,0,sizeof *s);s->RangeStatus=(uint8_t)sample_status;s->RangeMilliMeter=(uint16_t)sample_mm;return 0; }
VL53L0X_Error VL53L0X_ClearInterruptMask(VL53L0X_DEV d,uint32_t mask)
{ (void)d;assert(mask==0);return 0; }
static void setup(enum fault f,unsigned latency,unsigned frozen)
{
    fault=f;ticks=phase_started=assert_duration=boot_duration=0;
    phase=assert_reads=boot_reads=release_writes=assert_writes=0;
    data_calls=start_calls=config_calls=sample_reads=0;
    read_latency=latency;frozen_clock=frozen;io_error=0;sample_status=0;sample_mm=260;
}
static void run(const char *name,enum fault f,int expected,unsigned latency,unsigned frozen)
{
    int mm=777, rc;
    setup(f,latency,frozen); rc=vl53l0x_init(); assert(rc==expected);
    assert(assert_reads<=VL53L0X_RESET_MAX_READS && boot_reads<=VL53L0X_RESET_MAX_READS);
    assert(assert_duration<=VL53L0X_RESET_TIMEOUT_US && boot_duration<=VL53L0X_RESET_TIMEOUT_US);
    if(expected) {
        assert(data_calls==0 && start_calls==0 && config_calls==0);
        if(f!=WRONG_CHIP) assert(release_writes>=1);
        assert(vl53l0x_read_mm(&mm)==-1 && mm==777 && sample_reads==0);
    } else {
        assert(data_calls==1 && start_calls==1 && assert_writes==1 && release_writes==1);
        assert(vl53l0x_read_mm(&mm)==0 && mm==260);
        /* A bad optical measurement remains invalid after recovery. */
        sample_status=4;sample_mm=8190;mm=777;
        assert(vl53l0x_read_mm(&mm)==-3 && mm==777);
    }
    if(frozen && f==ASSERT_STUCK) assert(assert_reads==50);
    if(frozen && f==BOOT_STUCK) assert(boot_reads==50);
    ++cases;printf("PASS %s: rc=%d reads=%u/%u phases=%llu/%llu us\n",name,rc,assert_reads,boot_reads,(unsigned long long)assert_duration,(unsigned long long)boot_duration);
}
int main(void)
{
    run("normal",OK,0,1000,0);
    run("never enters reset",ASSERT_STUCK,-6,1000,0);
    run("never boots",BOOT_STUCK,-7,1000,0);
    run("assert write NACK",ASSERT_WRITE_NACK,-5,1000,0);
    run("release write NACK",RELEASE_WRITE_NACK,-5,1000,0);
    run("assert read NACK",ASSERT_READ_NACK,-5,1000,0);
    run("boot read NACK",BOOT_READ_NACK,-5,1000,0);
    run("wrong initial chip",WRONG_CHIP,-1,1000,0);
    run("wrong reboot ID",WRONG_BOOT_ID,-7,1000,0);
    run("assert deadline slow I/O",ASSERT_STUCK,-6,4000,0);
    run("boot deadline slow I/O",BOOT_STUCK,-7,4000,0);
    run("assert count cap stopped clock",ASSERT_STUCK,-6,0,1);
    run("boot count cap stopped clock",BOOT_STUCK,-7,0,1);
    run("valid assert response at deadline rejected",OK,-6,100000,0);
    run("valid boot response at deadline rejected",LATE_BOOT,-7,1000,0);
    run("success after prior failures",OK,0,1000,0);
    printf("%u bounded reset/integration cases passed\n",cases);return 0;
}
