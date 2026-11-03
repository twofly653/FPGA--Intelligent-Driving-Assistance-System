#ifndef APP_VL53L0X_PLATFORM_STATUS_H
#define APP_VL53L0X_PLATFORM_STATUS_H

/* ST's API occasionally overwrites an earlier platform error with a later
 * successful write. Keep a sticky I2C flag across one public API operation. */
void vl53l0x_platform_clear_io_error(void);
int vl53l0x_platform_io_error(void);

#endif
