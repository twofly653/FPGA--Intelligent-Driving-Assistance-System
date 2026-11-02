#ifndef APP_VL53L0X_RESET_H
#define APP_VL53L0X_RESET_H
#include "vl53l0x_platform.h"

#define VL53L0X_RESET_ERROR_IO             (-5)
#define VL53L0X_RESET_ERROR_ASSERT_TIMEOUT (-6)
#define VL53L0X_RESET_ERROR_BOOT_TIMEOUT   (-7)
#define VL53L0X_RESET_TIMEOUT_US           100000u
#define VL53L0X_RESET_MAX_READS            50u

typedef struct {
    unsigned stage; /* 0 idle, 1 assert write, 2 assert poll, 3 release, 4 boot, 5 ready */
    int result;
    int first_api_status;
    unsigned assert_reads;
    unsigned boot_reads;
    unsigned read_nacks;
    unsigned release_attempts;
} vl53l0x_reset_diagnostic_t;

/* Opt-in startup recovery only, never called repeatedly for bad range quality.
 * Read-side control-interface errors are retried within both fixed bounds.
 * Success requires acknowledged reset ID=0 and boot ID=0xEEAA.
 * Failures always attempt release. The caller clears sticky platform I/O only
 * after success, preserving the transient count in the diagnostic snapshot. */
int vl53l0x_reset_bounded(VL53L0X_DEV dev);
const vl53l0x_reset_diagnostic_t *vl53l0x_reset_diagnostic(void);
#endif
