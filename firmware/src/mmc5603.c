#include "mmc5603.h"
#include "i2c_bitbang.h"
#include "sleep.h"
#include <string.h>

#define MMC_ADDR 0x30u
#define SELFTEST_MAX_POLLS 64u
static mmc5603_diagnostic_t diagnostic;

void mmc5603_get_diagnostic(mmc5603_diagnostic_t *out)
{
    if (out) *out = diagnostic;
}

const char *mmc5603_selftest_result_name(mmc5603_selftest_result_t result)
{
    switch (result) {
    case MMC5603_SELFTEST_PASS: return "PASS";
    case MMC5603_SELFTEST_SAT_FAIL: return "SAT_FAIL";
    case MMC5603_SELFTEST_INCONCLUSIVE: return "INCONCLUSIVE";
    case MMC5603_SELFTEST_IO_ERROR: return "IO_ERROR";
    case MMC5603_SELFTEST_TIMEOUT: return "TIMEOUT";
    default: return "NOT_RUN";
    }
}

int mmc5603_reprobe_allowed(void)
{
    return diagnostic.result == MMC5603_SELFTEST_NOT_RUN ||
           diagnostic.result == MMC5603_SELFTEST_PASS ||
           diagnostic.result == MMC5603_SELFTEST_IO_ERROR;
}

static int restore_on_demand(void)
{
    u8 discard[9];
    int ok = 1;
    /* Best effort on EVERY operation; one failed transaction must not prevent
     * an attempt to switch off the self-test or continuous-mode controls. */
    if (i2c_reg_read_n(MMC_ADDR, 0x00u, discard, 9) != 0) ok = 0;
    if (i2c_reg_write(MMC_ADDR, 0x1Du, 0x00u) != 0) ok = 0;
    if (i2c_reg_write(MMC_ADDR, 0x1Cu, 0x00u) != 0) ok = 0;
    if (i2c_reg_write(MMC_ADDR, 0x1Au, 0x00u) != 0) ok = 0;
    if (i2c_reg_write(MMC_ADDR, 0x1Bu, 0x20u) != 0) ok = 0;
    return ok;
}

mmc5603_selftest_result_t mmc5603_run_selftest(void)
{
    u8 discard[9], status = 0;
    unsigned i;
    mmc5603_selftest_result_t result = MMC5603_SELFTEST_IO_ERROR;
    memset(&diagnostic, 0, sizeof diagnostic);
    /* MEMSIC MMC5603NJ Rev.B p14: factory ST bytes, direct 80% thresholds,
     * then Auto_st_en | TM_M = 0x41. No undocumented ST-128 transform.
     * https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5603NJDatasheetRev.B.pdf
     */
    if (i2c_reg_read_n(MMC_ADDR, 0x27u, diagnostic.factory, 3) != 0)
        goto restore;
    for (i = 0; i < 3; ++i) {
        diagnostic.threshold[i] = (u8)((unsigned)diagnostic.factory[i]*4u/5u);
        /* No manufacturer range is specified. Treat empty / all-ones endpoint
         * values conservatively as INCONCLUSIVE, never as proof of damage.
         * In particular, zero thresholds must not manufacture a PASS. */
        if (diagnostic.factory[i] == 0 || diagnostic.factory[i] == 0xffu ||
            diagnostic.threshold[i] == 0) {
            result = MMC5603_SELFTEST_INCONCLUSIVE;
            goto restore;
        }
    }
    for (i = 0; i < 3; ++i)
        if (i2c_reg_write(MMC_ADDR, (u8)(0x1Eu+i), diagnostic.threshold[i]) != 0)
            goto restore;
    /* Clear any old measurement-ready flag BEFORE initiating the new test. */
    if (i2c_reg_read_n(MMC_ADDR, 0x00u, discard, 9) != 0 ||
        i2c_reg_write(MMC_ADDR, 0x1Bu, 0x41u) != 0)
        goto restore;
    result = MMC5603_SELFTEST_TIMEOUT;
    for (i = 0; i < SELFTEST_MAX_POLLS; ++i) {
        usleep(1000);
        ++diagnostic.polls;
        if (i2c_reg_read(MMC_ADDR, 0x18u, &status) != 0) {
            result = MMC5603_SELFTEST_IO_ERROR;
            break;
        }
        diagnostic.status = status;
        if (status & 0x40u) {
            result = (status & 0x20u) ? MMC5603_SELFTEST_SAT_FAIL :
                                      MMC5603_SELFTEST_PASS;
            break;
        }
    }
restore:
    diagnostic.restore_ok = restore_on_demand();
    if (!diagnostic.restore_ok) result = MMC5603_SELFTEST_IO_ERROR;
    diagnostic.result = result;
    return result;
}

static int raw_to_mg(const u8 *data, int low_index, int fine_index)
{
    u32 raw = ((u32)data[low_index] << 12) |
              ((u32)data[low_index + 1] << 4) |
              ((u32)data[fine_index] >> 4);
    return ((int)raw - 524288) * 1000 / 16384;
}

int mmc5603_init(void)
{
    u8 id;
    memset(&diagnostic, 0, sizeof diagnostic);
    diagnostic.result = MMC5603_SELFTEST_IO_ERROR;
    if (i2c_reg_read(MMC_ADDR, 0x39u, &id) != 0 || id != 0x10u)
        return -1;
    /* A JTAG CPU restart does not reset this independently powered chip.
     * Establish known on-demand mode even after another application's CMM or
     * self-test configuration. MEMSIC Rev.B defines these control bits;
     * the 20 ms reset / 1 ms SET-RESET waits follow Adafruit's public driver:
     * https://github.com/adafruit/Adafruit_MMC56x3/blob/master/Adafruit_MMC56x3.cpp
     * This startup conditioning is not proof that external magnetic fields
     * are clean; each normal conversion still uses the automatic SR feature. */
    if (i2c_reg_write(MMC_ADDR, 0x1Cu, 0x80u) != 0) return -1;
    usleep(20000);
    if (i2c_reg_read(MMC_ADDR, 0x39u, &id) != 0 || id != 0x10u)
        return -1;
    if (i2c_reg_write(MMC_ADDR, 0x1Du, 0x00u) != 0 || /* CMM / hpower off */
        i2c_reg_write(MMC_ADDR, 0x1Cu, 0x00u) != 0 || /* XYZ on, ST off, BW00 */
        i2c_reg_write(MMC_ADDR, 0x1Au, 0x00u) != 0 || /* no continuous ODR */
        i2c_reg_write(MMC_ADDR, 0x1Bu, 0x00u) != 0)   /* no self-test/trigger */
        return -1;
    if (i2c_reg_write(MMC_ADDR, 0x1Bu, 0x08u) != 0) return -1;
    usleep(1000); /* SET pulse recovery */
    if (i2c_reg_write(MMC_ADDR, 0x1Bu, 0x10u) != 0) return -1;
    usleep(1000); /* RESET pulse recovery */
    if (i2c_reg_write(MMC_ADDR, 0x1Bu, 0x20u) != 0) return -1;
    return mmc5603_run_selftest() == MMC5603_SELFTEST_PASS ? 0 : -1;
}

int mmc5603_start(void)
{
    if (diagnostic.result != MMC5603_SELFTEST_PASS || !diagnostic.restore_ok)
        return -1;
    return i2c_reg_write(MMC_ADDR, 0x1Bu, 0x21u); /* TM_M + Auto_SR_en */
}

int mmc5603_try_fetch(mmc5603_sample_t *sample)
{
    u8 status, data[9];
    mmc5603_sample_t next;
    if (!sample || i2c_reg_read(MMC_ADDR, 0x18u, &status) != 0)
        return -1;
    if (!(status & 0x40u))
        return 1;
    if (i2c_reg_read_n(MMC_ADDR, 0x00u, data, 9) != 0)
        return -1;
    next.x_mg = raw_to_mg(data, 0, 6);
    next.y_mg = raw_to_mg(data, 2, 7);
    next.z_mg = raw_to_mg(data, 4, 8);
    *sample = next;
    return 0;
}

/* Legacy synchronous API retained for source compatibility. The integrated
 * application uses start/try_fetch exclusively and never sleeps for a sample. */
int mmc5603_read(mmc5603_sample_t *sample)
{
    int i, result;
    if (!sample || mmc5603_start() != 0)
        return -1;
    for (i = 0; i < 40; ++i) {
        result = mmc5603_try_fetch(sample);
        if (result <= 0)
            return result;
        usleep(200);
    }
    return -1;
}
