#ifndef MMC5603_H
#define MMC5603_H

#include "xil_types.h"

typedef struct {
    /* Signed milligauss. Values require calibration before heading use. */
    int x_mg;
    int y_mg;
    int z_mg;
} mmc5603_sample_t;

typedef enum {
    MMC5603_SELFTEST_NOT_RUN = 0,
    MMC5603_SELFTEST_PASS = 1,
    MMC5603_SELFTEST_SAT_FAIL = 2,
    MMC5603_SELFTEST_INCONCLUSIVE = 3,
    MMC5603_SELFTEST_IO_ERROR = 4,
    MMC5603_SELFTEST_TIMEOUT = 5
} mmc5603_selftest_result_t;

typedef struct {
    mmc5603_selftest_result_t result;
    u8 factory[3], threshold[3], status;
    unsigned polls;
    int restore_ok;
} mmc5603_diagnostic_t;

int mmc5603_init(void);
/* Synchronous STARTUP diagnostic only: caller must stop normal acquisition.
 * PASS checks built-in coil response, NOT north accuracy or external fields.
 * init runs this after reset; all non-PASS results make init/start fail. */
mmc5603_selftest_result_t mmc5603_run_selftest(void);
void mmc5603_get_diagnostic(mmc5603_diagnostic_t *diagnostic);
const char *mmc5603_selftest_result_name(mmc5603_selftest_result_t result);
/* Owner may retry transient bus faults, but should not repeatedly stop IMU/UI
 * for a persistent startup self-test result. Explicit init always retries. */
int mmc5603_reprobe_allowed(void);
int mmc5603_read(mmc5603_sample_t *sample);
/* Nonblocking measurement pair: start once, then try_fetch from main loop.
 * start: 0 success, -1 error. try_fetch: 0 new data, 1 busy, -1 error.
 * The caller owns the conversion timeout and must not restart while waiting.
 */
int mmc5603_start(void);
int mmc5603_try_fetch(mmc5603_sample_t *sample);

#endif
