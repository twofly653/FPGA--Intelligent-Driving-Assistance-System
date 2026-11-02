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

static VL53L0X_Dev_t g_sensor;
static int g_running;

int vl53l0x_init(void)
{
    uint16_t chip_id;
    uint8_t vhv_settings;
    uint8_t phase_cal;
    uint8_t aperture_spads;
    uint32_t ref_spad_count;
    int reset_result;

    if (g_running) {
        g_running = 0;
        /* Best effort: the sensor may already have reset or been unplugged. */
        (void)VL53L0X_StopMeasurement(&g_sensor);
    }
    memset(&g_sensor, 0, sizeof(g_sensor));
    g_sensor.I2cDevAddr = VL53L0X_ADDR7;

    vl53l0x_platform_clear_io_error();
    if (VL53L0X_RdWord(&g_sensor, 0xC0u, &chip_id) != 0 ||
        vl53l0x_platform_io_error() ||
        chip_id != VL53L0X_CHIP_ID)
        return -1;
    /* CPU/JTAG reload clears g_running while the separately powered sensor
     * can retain its old continuous-ranging state. Establish a fresh sensor
     * state before running ST calibration, with bounded waits and no retry. */
    reset_result = vl53l0x_reset_bounded(&g_sensor);
    if (reset_result != 0) return reset_result;
    if (VL53L0X_DataInit(&g_sensor) != 0 ||
        VL53L0X_StaticInit(&g_sensor) != 0 ||
        VL53L0X_PerformRefCalibration(&g_sensor,
                                      &vhv_settings, &phase_cal) != 0 ||
        VL53L0X_PerformRefSpadManagement(&g_sensor,
                                         &ref_spad_count,
                                         &aperture_spads) != 0 ||
        VL53L0X_SetLimitCheckEnable(&g_sensor,
                                     VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE,
                                     1) != 0 ||
        VL53L0X_SetLimitCheckEnable(&g_sensor,
                                     VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE,
                                     1) != 0 ||
        VL53L0X_SetLimitCheckValue(&g_sensor,
                                    VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE,
                                    6554u) != 0 ||
        VL53L0X_SetLimitCheckValue(&g_sensor,
                                    VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE,
                                    60u << 16) != 0 ||
        VL53L0X_SetMeasurementTimingBudgetMicroSeconds(&g_sensor,
                                                       33000u) != 0 ||
        VL53L0X_SetVcselPulsePeriod(&g_sensor,
                                     VL53L0X_VCSEL_PERIOD_PRE_RANGE, 18) != 0 ||
        VL53L0X_SetVcselPulsePeriod(&g_sensor,
                                     VL53L0X_VCSEL_PERIOD_FINAL_RANGE, 14) != 0 ||
        VL53L0X_SetDeviceMode(&g_sensor,
                               VL53L0X_DEVICEMODE_CONTINUOUS_RANGING) != 0 ||
        VL53L0X_StartMeasurement(&g_sensor) != 0 ||
        vl53l0x_platform_io_error())
        return -4;

    g_running = 1;
    return 0;
}

int vl53l0x_read_mm(int *mm)
{
    uint8_t ready = 0;
    VL53L0X_RangingMeasurementData_t sample;
    VL53L0X_Error status;

    if (!mm || !g_running)
        return -1;
    vl53l0x_platform_clear_io_error();
    status = VL53L0X_GetMeasurementDataReady(&g_sensor, &ready);
    if (status != VL53L0X_ERROR_NONE || vl53l0x_platform_io_error())
        return -4;
    if (!ready)
        return -2;
    status = VL53L0X_GetRangingMeasurementData(&g_sensor, &sample);
    if (status != VL53L0X_ERROR_NONE || vl53l0x_platform_io_error())
        return -4;
    status = VL53L0X_ClearInterruptMask(&g_sensor, 0);
    if (status != VL53L0X_ERROR_NONE || vl53l0x_platform_io_error())
        return -4;
    if (sample.RangeStatus != 0)
        return -3;
    *mm = (int)sample.RangeMilliMeter;
    return 0;
}
