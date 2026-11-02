#ifndef APP_VL53L0X_H
#define APP_VL53L0X_H
#include <stdint.h>

/* Diagnostic stage: 1 identity, 2 optional reset, 3 DataInit, 4 StaticInit,
 * 5 ref calibration, 6 ref SPAD, 7/8 check-enable, 9/10 check-value,
 * 11 timing, 12/13 VCSEL, 14 device mode, 15 start, 16 ready.
 * A zero RangeStatus remains mandatory for valid measurements. */
typedef struct {
    unsigned stage;
    int result;
    int api_status;
    int io_error;
    int reset_enabled;
    int reset_io_seen;
    unsigned reset_read_nacks;
} vl53l0x_init_diagnostic_t;
const vl53l0x_init_diagnostic_t *vl53l0x_init_diagnostic(void);

/* Start the VL53L0X in continuous ranging mode. Call after i2c_init().
 * Calibration is synchronous and can take seconds on failure. Use only at startup
 * or for rate-limited recovery; this is not a nonblocking UI operation.
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

typedef struct {
    int last_return,api_status,io_error,has_sample,last_read_frame;
    int range_status,raw_mm;
    uint32_t signal_mcps_x65536,ambient_mcps_x65536;
} vl53l0x_runtime_diagnostic_t;
/* Snapshot only: never performs bus I/O. Raw distance may be invalid. */
void vl53l0x_get_runtime_diagnostic(vl53l0x_runtime_diagnostic_t *out);
#endif
