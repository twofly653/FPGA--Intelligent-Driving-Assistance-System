/* Application wrapper around ST's VL53L0X API (STSW-IMG005).
 * ST API source: https://github.com/zephyrproject-rtos/hal_st/tree/ce795a9b0de623022c17d2ff394587c024aec850/sensor/vl53l0x/api/core
 */
#include "vl53l0x.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform_status.h"
#include "vl53l0x_reset.h"
#include <string.h>

#define VL53L0X_ADDR7 0x29u
#define VL53L0X_CHIP_ID 0xEEAAu
/* On by default: the sensor keeps 3.3 V across a PS-only reset, so without an
 * explicit reset at init it can stay in a saturated state where AmbientRate
 * pins at ~9.4 Mcps and every echo comes back as a self-reflection at 33-62 mm.
 * That is exactly what the bench board did for a whole afternoon; one reset at
 * init drops ambient to ~0.15 Mcps and restores real ranging. */
#ifndef APP_TOF_SOFT_RESET
#define APP_TOF_SOFT_RESET 1
#endif

static VL53L0X_Dev_t g_sensor;
static int g_running;
static vl53l0x_runtime_diagnostic_t g_runtime_diag;
void vl53l0x_get_runtime_diagnostic(vl53l0x_runtime_diagnostic_t *out)
{ if(out) *out=g_runtime_diag; }
static int read_result(int result,VL53L0X_Error status)
{
    g_runtime_diag.last_return=result;
    g_runtime_diag.api_status=status;
    g_runtime_diag.io_error=vl53l0x_platform_io_error();
    return result;
}
static vl53l0x_init_diagnostic_t g_init_diag;

const vl53l0x_init_diagnostic_t *vl53l0x_init_diagnostic(void)
{
    return &g_init_diag;
}

static int init_failure(int result, VL53L0X_Error status)
{
    g_init_diag.result = result;
    g_init_diag.api_status = status;
    g_init_diag.io_error = vl53l0x_platform_io_error();
    g_runtime_diag.api_status=status;
    g_runtime_diag.io_error=g_init_diag.io_error;
    return result;
}

/* Check each ST call before advancing: some vendor calls can overwrite an
 * intermediate return status, while the platform retains any I2C error. */
#define INIT_CALL(stage_id, expression) do { \
    g_init_diag.stage = (stage_id); \
    status = (expression); \
    if (status != VL53L0X_ERROR_NONE || vl53l0x_platform_io_error()) \
        return init_failure(-4, status); \
} while (0)

int vl53l0x_init(void)
{
    uint16_t chip_id = 0;
    uint8_t vhv_settings;
    uint8_t phase_cal;
    uint8_t aperture_spads;
    uint32_t ref_spad_count;
    VL53L0X_Error status;

    memset(&g_runtime_diag,0,sizeof g_runtime_diag);
    g_runtime_diag.last_return=-1;
    g_runtime_diag.range_status=255;
    g_runtime_diag.raw_mm=-1;
    memset(&g_init_diag, 0, sizeof(g_init_diag));
    g_init_diag.reset_enabled = APP_TOF_SOFT_RESET;
    if (g_running) {
        g_running = 0;
        (void)VL53L0X_StopMeasurement(&g_sensor);
    }
    memset(&g_sensor, 0, sizeof(g_sensor));
    g_sensor.I2cDevAddr = VL53L0X_ADDR7;

    vl53l0x_platform_clear_io_error();
    g_init_diag.stage = 1;
    status = VL53L0X_RdWord(&g_sensor, 0xC0u, &chip_id);
    if (status != VL53L0X_ERROR_NONE || vl53l0x_platform_io_error() ||
        chip_id != VL53L0X_CHIP_ID)
        return init_failure(-1, status);
#if APP_TOF_SOFT_RESET
    {
        int reset_result;
        const vl53l0x_reset_diagnostic_t *reset_diag;
        g_init_diag.stage = 2;
        reset_result = vl53l0x_reset_bounded(&g_sensor);
        reset_diag = vl53l0x_reset_diagnostic();
        g_init_diag.reset_io_seen = vl53l0x_platform_io_error();
        g_init_diag.reset_read_nacks = reset_diag->read_nacks;
        if (reset_result != 0)
            return init_failure(reset_result,
                                (VL53L0X_Error)reset_diag->first_api_status);
        /* Only confirmed release + full boot identity permit discarding
         * transient reset I/O. The snapshot above retains the evidence. */
        vl53l0x_platform_clear_io_error();
    }
#endif
    INIT_CALL(3, VL53L0X_DataInit(&g_sensor));
    INIT_CALL(4, VL53L0X_StaticInit(&g_sensor));
    INIT_CALL(5, VL53L0X_PerformRefCalibration(&g_sensor,
                                            &vhv_settings, &phase_cal));
    INIT_CALL(6, VL53L0X_PerformRefSpadManagement(&g_sensor,
                                               &ref_spad_count, &aperture_spads));
    INIT_CALL(7, VL53L0X_SetLimitCheckEnable(&g_sensor,
                     VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, 1));
    INIT_CALL(8, VL53L0X_SetLimitCheckEnable(&g_sensor,
                     VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, 1));
    /* 0.05 Mcps instead of the 0.1 Mcps default: at ~9 Mcps ambient the
     * echoes that survive are weak, and rejecting them cost ~8 valid frames
     * per thousand. The 60 mm sigma check still guards against noise. */
    INIT_CALL(9, VL53L0X_SetLimitCheckValue(&g_sensor,
                     VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, 3277u));
    INIT_CALL(10, VL53L0X_SetLimitCheckValue(&g_sensor,
                     VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, 60u << 16));
    /* Long-range VCSEL pulses must be programmed BEFORE the timing budget:
     * SetVcselPulsePeriod rescales the budget internally, so setting it last
     * silently discards whatever budget was requested. */
    INIT_CALL(11, VL53L0X_SetVcselPulsePeriod(&g_sensor,
                                           VL53L0X_VCSEL_PERIOD_PRE_RANGE, 18));
    INIT_CALL(12, VL53L0X_SetVcselPulsePeriod(&g_sensor,
                                           VL53L0X_VCSEL_PERIOD_FINAL_RANGE, 14));
    /* 33 ms is the stock budget; tripling it raises the photon count on a weak
     * echo roughly 3x (SNR ~ sqrt(T)) which is what actually recovers readings
     * from a far/small target in a bright room. Still 10 measurements/s. */
    INIT_CALL(13, VL53L0X_SetMeasurementTimingBudgetMicroSeconds(&g_sensor, 100000u));
    INIT_CALL(14, VL53L0X_SetDeviceMode(&g_sensor,
                                      VL53L0X_DEVICEMODE_CONTINUOUS_RANGING));
    INIT_CALL(15, VL53L0X_StartMeasurement(&g_sensor));
    g_running = 1;
    g_init_diag.stage = 16;
    g_runtime_diag.last_return=-2;
    return 0;
}

int vl53l0x_read_mm(int *mm)
{
    uint8_t ready=0;
    VL53L0X_RangingMeasurementData_t sample;
    VL53L0X_Error status;
    g_runtime_diag.last_read_frame=0;
    if(!mm || !g_running) return read_result(-1,VL53L0X_ERROR_NONE);
    vl53l0x_platform_clear_io_error();
    status=VL53L0X_GetMeasurementDataReady(&g_sensor,&ready);
    if(status!=VL53L0X_ERROR_NONE || vl53l0x_platform_io_error())
        return read_result(-4,status);
    if(!ready) return read_result(-2,status);
    status=VL53L0X_GetRangingMeasurementData(&g_sensor,&sample);
    if(status!=VL53L0X_ERROR_NONE || vl53l0x_platform_io_error())
        return read_result(-4,status);
    g_runtime_diag.has_sample=1;
    g_runtime_diag.range_status=sample.RangeStatus;
    g_runtime_diag.raw_mm=sample.RangeMilliMeter;
    g_runtime_diag.signal_mcps_x65536=sample.SignalRateRtnMegaCps;
    g_runtime_diag.ambient_mcps_x65536=sample.AmbientRateRtnMegaCps;
    status=VL53L0X_ClearInterruptMask(&g_sensor,0);
    if(status!=VL53L0X_ERROR_NONE || vl53l0x_platform_io_error())
        return read_result(-4,status);
    g_runtime_diag.last_read_frame=1;
    if(sample.RangeStatus!=0) return read_result(-3,status);
    *mm=(int)sample.RangeMilliMeter;
    return read_result(0,status);
}
