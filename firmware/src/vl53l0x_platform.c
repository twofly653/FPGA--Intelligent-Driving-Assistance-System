/* VL53L0X ST API platform I/O for the project's bit-banged I2C bus. */
#include "vl53l0x_platform.h"
#include "i2c_bitbang.h"
#include "vl53l0x_platform_status.h"
#include "sleep.h"

#define VL53L0X_WRITE_CHUNK 32u

static int g_io_error;

void vl53l0x_platform_clear_io_error(void)
{
    g_io_error = 0;
}

int vl53l0x_platform_io_error(void)
{
    return g_io_error;
}

VL53L0X_Error VL53L0X_WriteMulti(VL53L0X_DEV dev, uint8_t index,
                                  uint8_t *data, uint32_t count)
{
    uint8_t frame[VL53L0X_WRITE_CHUNK + 1u];
    uint32_t offset = 0;
    uint32_t n;
    uint32_t i;

    if (!dev || (!data && count) || count > 256u - index)
        return VL53L0X_ERROR_INVALID_PARAMS;
    while (offset < count) {
        n = count - offset;
        if (n > VL53L0X_WRITE_CHUNK)
            n = VL53L0X_WRITE_CHUNK;
        frame[0] = (uint8_t)(index + offset);
        for (i = 0; i < n; ++i)
            frame[i + 1u] = data[offset + i];
        if (i2c_write(dev->I2cDevAddr, frame, (int)(n + 1u)) != 0) {
            g_io_error = 1;
            return VL53L0X_ERROR_CONTROL_INTERFACE;
        }
        offset += n;
    }
    return VL53L0X_ERROR_NONE;
}

VL53L0X_Error VL53L0X_ReadMulti(VL53L0X_DEV dev, uint8_t index,
                                 uint8_t *data, uint32_t count)
{
    uint32_t offset = 0;
    uint32_t n;
    uint8_t reg;

    if (!dev || (!data && count) || count > 256u - index)
        return VL53L0X_ERROR_INVALID_PARAMS;
    while (offset < count) {
        n = count - offset;
        if (n > VL53L0X_WRITE_CHUNK)
            n = VL53L0X_WRITE_CHUNK;
        reg = (uint8_t)(index + offset);
        if (i2c_write_read(dev->I2cDevAddr, &reg, 1,
                           data + offset, (int)n) != 0) {
            g_io_error = 1;
            return VL53L0X_ERROR_CONTROL_INTERFACE;
        }
        offset += n;
    }
    return VL53L0X_ERROR_NONE;
}

VL53L0X_Error VL53L0X_WrByte(VL53L0X_DEV dev, uint8_t index, uint8_t value)
{
    return VL53L0X_WriteMulti(dev, index, &value, 1);
}

VL53L0X_Error VL53L0X_WrWord(VL53L0X_DEV dev, uint8_t index, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return VL53L0X_WriteMulti(dev, index, bytes, 2);
}

VL53L0X_Error VL53L0X_WrDWord(VL53L0X_DEV dev, uint8_t index, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 8), (uint8_t)value};
    return VL53L0X_WriteMulti(dev, index, bytes, 4);
}

VL53L0X_Error VL53L0X_RdByte(VL53L0X_DEV dev, uint8_t index, uint8_t *value)
{
    return VL53L0X_ReadMulti(dev, index, value, 1);
}

VL53L0X_Error VL53L0X_RdWord(VL53L0X_DEV dev, uint8_t index, uint16_t *value)
{
    uint8_t bytes[2];
    VL53L0X_Error status;
    if (!value)
        return VL53L0X_ERROR_INVALID_PARAMS;
    status = VL53L0X_ReadMulti(dev, index, bytes, 2);
    if (status == VL53L0X_ERROR_NONE)
        *value = ((uint16_t)bytes[0] << 8) | bytes[1];
    return status;
}

VL53L0X_Error VL53L0X_RdDWord(VL53L0X_DEV dev, uint8_t index, uint32_t *value)
{
    uint8_t bytes[4];
    VL53L0X_Error status;
    if (!value)
        return VL53L0X_ERROR_INVALID_PARAMS;
    status = VL53L0X_ReadMulti(dev, index, bytes, 4);
    if (status == VL53L0X_ERROR_NONE)
        *value = ((uint32_t)bytes[0] << 24) |
                 ((uint32_t)bytes[1] << 16) |
                 ((uint32_t)bytes[2] << 8) | bytes[3];
    return status;
}

VL53L0X_Error VL53L0X_UpdateByte(VL53L0X_DEV dev, uint8_t index,
                                   uint8_t and_data, uint8_t or_data)
{
    uint8_t value;
    VL53L0X_Error status = VL53L0X_RdByte(dev, index, &value);
    if (status != VL53L0X_ERROR_NONE)
        return status;
    return VL53L0X_WrByte(dev, index, (uint8_t)((value & and_data) | or_data));
}

VL53L0X_Error VL53L0X_PollingDelay(VL53L0X_DEV dev)
{
    (void)dev;
    usleep(2000);
    return VL53L0X_ERROR_NONE;
}
