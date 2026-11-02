#include "vl53l0x_reset.h"
#include "xtime_l.h"
#include "sleep.h"

#define RESET_REG 0xBFu
#define MODEL_ID_REG 0xC0u
#define CHIP_ID 0xEEAAu
#define POLL_DELAY_US 2000u
/* A register read on this project's bounded, 5-us bit-banged bus takes
 * less than 1 ms. Reserve 5 ms before the deadline for that synchronous
 * transaction; never start a read in the last part of the budget. */
#define IO_RESERVE_US 5000u

static unsigned elapsed_us(XTime start)
{
    XTime now, delta;
    XTime_GetTime(&now);
    delta = now - start;
    if (delta >= COUNTS_PER_SECOND) return VL53L0X_RESET_TIMEOUT_US;
    return (unsigned)((delta * 1000000ULL) / COUNTS_PER_SECOND);
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
        if (booting) status = VL53L0X_RdWord(dev, MODEL_ID_REG, &id);
        else status = VL53L0X_RdByte(dev, MODEL_ID_REG, &byte);
        if (status != VL53L0X_ERROR_NONE) return VL53L0X_RESET_ERROR_IO;
        /* A late response must not be accepted as a successful reset. */
        if (elapsed_us(start) >= VL53L0X_RESET_TIMEOUT_US) return timeout;
        if ((booting && id == CHIP_ID) || (!booting && byte == 0u)) return 0;
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
    VL53L0X_Error release;
    if (!dev) return VL53L0X_RESET_ERROR_IO;
    if (VL53L0X_WrByte(dev, RESET_REG, 0u) != VL53L0X_ERROR_NONE) {
        /* Even an ACK failure can follow a write that the device received. */
        (void)VL53L0X_WrByte(dev, RESET_REG, 1u);
        return VL53L0X_RESET_ERROR_IO;
    }
    result = wait_identity(dev, 0);
    if (result == 0) usleep(POLL_DELAY_US);
    release = VL53L0X_WrByte(dev, RESET_REG, 1u);
    if (result != 0) return result;
    if (release != VL53L0X_ERROR_NONE) {
        (void)VL53L0X_WrByte(dev, RESET_REG, 1u);
        return VL53L0X_RESET_ERROR_IO;
    }
    result = wait_identity(dev, 1);
    if (result != 0) {
        (void)VL53L0X_WrByte(dev, RESET_REG, 1u);
        return result;
    }
    usleep(POLL_DELAY_US);
    return 0;
}
