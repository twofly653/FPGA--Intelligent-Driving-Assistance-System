#include "vl53l0x_probe.h"
#include "i2c_bitbang.h"

/* ST reference registers after a fresh reset: C0=EE, C1=AA, C2=10. */
int vl53l0x_probe(void)
{
    u8 model, revision;
    if (i2c_reg_read(0x29u, 0xC0u, &model) != 0 ||
        i2c_reg_read(0x29u, 0xC2u, &revision) != 0)
        return -1;
    return (model == 0xEEu && revision == 0x10u) ? 0 : -1;
}
