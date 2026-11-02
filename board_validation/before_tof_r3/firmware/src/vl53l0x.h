#ifndef APP_VL53L0X_H
#define APP_VL53L0X_H

/* Start the VL53L0X in continuous ranging mode. Call after i2c_init().
 * Calibration can take time; call once during sensor startup, not in the UI loop.
 * Returns 0 on success, a negative code on failure.
 */
int vl53l0x_init(void);

/* Poll for a fresh sample without waiting for the measurement to finish.
 * Returns 0 and writes millimeters only when ST's RangeStatus is 0.
 * -1: invalid argument or not initialized
 * -2: no fresh sample yet
 * -3: sample reported an invalid range
 * -4: I2C or ST API failure
 */
int vl53l0x_read_mm(int *mm);

#endif
