#define _GNU_SOURCE

#include "healthy.h"

#include "common.h"
#include "util.h"

int hy_version(hy_t *h, uint8_t *out)
{
    uint8_t payload[1] = { 0x50 };

    return ec_xact(h->ec, CMD_VERSION, payload, 1, true, out);
}

int hy_read(hy_t *h, uint8_t reg, uint8_t *out)
{
    uint8_t payload[3] = { TBL_READ, reg, 0x00 };

    return ec_xact(h->ec, CMD_TABLE, payload, 3, true, out);
}

int hy_write(hy_t *h, uint8_t reg, uint8_t value)
{
    uint8_t payload[3] = { TBL_WRITE, reg, value };

    if (ec_xact(h->ec, CMD_TABLE, payload, 3, false, NULL))
        return -1;
    nsleep(h->gap);
    return 0;
}

int hy_select(hy_t *h, int index)
{
    return hy_write(h, REG_FAN_INDEX, (uint8_t)index);
}

int hy_rpm(hy_t *h, int *out)
{
    uint8_t hi, lo;

    if (hy_read(h, REG_RPM_HI, &hi) || hy_read(h, REG_RPM_LO, &lo))
        return -1;
    *out = (hi << 8) | lo;
    return 0;
}
