#include "vl53l0x_reset.h"
#include "xtime_l.h"
#include "sleep.h"
#include <string.h>

#define RESET_REG 0xBFu
#define MODEL_ID_REG 0xC0u
#define CHIP_ID 0xEEAAu
#define POLL_DELAY_US 2000u
/* The bounded project I2C transaction takes less than 1 ms. */
#define IO_RESERVE_US 5000u

static vl53l0x_reset_diagnostic_t g_reset_diag;

const vl53l0x_reset_diagnostic_t *vl53l0x_reset_diagnostic(void)
{
    return &g_reset_diag;
}

static void remember_error(VL53L0X_Error status)
{
    if (status != VL53L0X_ERROR_NONE && g_reset_diag.first_api_status == 0)
        g_reset_diag.first_api_status = status;
}

static unsigned elapsed_us(XTime start)
{
    XTime now, delta;
    XTime_GetTime(&now);
    delta = now - start;
    if (delta >= COUNTS_PER_SECOND) return VL53L0X_RESET_TIMEOUT_US;
    return (unsigned)((delta * 1000000ULL) / COUNTS_PER_SECOND);
}

static VL53L0X_Error release_reset(VL53L0X_DEV dev)
{
    VL53L0X_Error status;
    ++g_reset_diag.release_attempts;
    status = VL53L0X_WrByte(dev, RESET_REG, 1u);
    remember_error(status);
    return status;
}

static int wait_identity(VL53L0X_DEV dev, int booting)
{
    XTime start;
    unsigned tries;
    int timeout = booting ? VL53L0X_RESET_ERROR_BOOT_TIMEOUT :
                            VL53L0X_RESET_ERROR_ASSERT_TIMEOUT;
    XTime_GetTime(&start);
    for (tries = 0; tries < VL53L0X_RESET_MAX_READS; ++tries) {
        VL53L0X_Error status;
        uint16_t id = 0;
        uint8_t byte = 0;
        if (elapsed_us(start) >= VL53L0X_RESET_TIMEOUT_US - IO_RESERVE_US)
            return timeout;
        if (booting) {
            ++g_reset_diag.boot_reads;
            status = VL53L0X_RdWord(dev, MODEL_ID_REG, &id);
        } else {
            ++g_reset_diag.assert_reads;
            status = VL53L0X_RdByte(dev, MODEL_ID_REG, &byte);
        }
        if (status != VL53L0X_ERROR_NONE) {
            remember_error(status);
            if (status != VL53L0X_ERROR_CONTROL_INTERFACE)
                return VL53L0X_RESET_ERROR_IO;
            ++g_reset_diag.read_nacks;
        }
        /* A NACK never counts as an identity match, even if the buffer is 0. */
        if (elapsed_us(start) >= VL53L0X_RESET_TIMEOUT_US) return timeout;
        if (status == VL53L0X_ERROR_NONE &&
            ((booting && id == CHIP_ID) || (!booting && byte == 0u))) return 0;
        if (tries + 1u == VL53L0X_RESET_MAX_READS) break;
        if (elapsed_us(start) >= VL53L0X_RESET_TIMEOUT_US - IO_RESERVE_US - POLL_DELAY_US)
            return timeout;
        usleep(POLL_DELAY_US);
    }
    return timeout;
}

int vl53l0x_reset_bounded(VL53L0X_DEV dev)
{
    int result;
    VL53L0X_Error status;
    memset(&g_reset_diag, 0, sizeof(g_reset_diag));
    if (!dev) {
        g_reset_diag.result = VL53L0X_RESET_ERROR_IO;
        return g_reset_diag.result;
    }
    g_reset_diag.stage = 1;
    status = VL53L0X_WrByte(dev, RESET_REG, 0u);
    if (status != VL53L0X_ERROR_NONE) {
        remember_error(status);
        (void)release_reset(dev);
        g_reset_diag.result = VL53L0X_RESET_ERROR_IO;
        return g_reset_diag.result;
    }
    g_reset_diag.stage = 2;
    result = wait_identity(dev, 0);
    if (result != 0) {
        (void)release_reset(dev);
        g_reset_diag.result = result;
        return result;
    }
    usleep(POLL_DELAY_US);
    g_reset_diag.stage = 3;
    status = release_reset(dev);
    if (status != VL53L0X_ERROR_NONE) {
        (void)release_reset(dev);
        g_reset_diag.result = VL53L0X_RESET_ERROR_IO;
        return g_reset_diag.result;
    }
    g_reset_diag.stage = 4;
    result = wait_identity(dev, 1);
    if (result != 0) {
        (void)release_reset(dev);
        g_reset_diag.result = result;
        return result;
    }
    usleep(POLL_DELAY_US);
    g_reset_diag.stage = 5;
    return 0;
}
