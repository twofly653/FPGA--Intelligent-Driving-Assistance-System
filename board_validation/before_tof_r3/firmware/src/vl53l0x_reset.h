#ifndef APP_VL53L0X_RESET_H
#define APP_VL53L0X_RESET_H
#include "vl53l0x_platform.h"

/* Distinct from the existing wrapper's -1/-2/-3/-4 results. */
#define VL53L0X_RESET_ERROR_IO             (-5)
#define VL53L0X_RESET_ERROR_ASSERT_TIMEOUT (-6)
#define VL53L0X_RESET_ERROR_BOOT_TIMEOUT   (-7)
#define VL53L0X_RESET_TIMEOUT_US           100000u
#define VL53L0X_RESET_MAX_READS            50u

/* Reset only the addressed ToF sensor after its identity has been checked.
 * No autonomous retries; failures always attempt to release soft reset.
 * Uses bounded polling rather than the ST API's unbounded ResetDevice(). */
int vl53l0x_reset_bounded(VL53L0X_DEV dev);
#endif
