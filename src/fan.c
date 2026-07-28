#define _GNU_SOURCE

#include "fan.h"

#include "common.h"
#include "util.h"

#include <stdlib.h>

int percent_to_duty(int percent)
{
    return (int)(percent * 255 / 100.0 + 0.5);
}

int duty_to_percent(int duty)
{
    return (int)(duty * 100 / 255.0 + 0.5);
}

bool duty_close(int readback_value, int want)
{
    int slack = want * TOL_PCT / 100;

    if (slack < TOL_MIN)
        slack = TOL_MIN;
    return abs(readback_value - want) <= slack;
}

const char *mode_name(int mode)
{
    if (mode < 0)
        return "unreadable";
    return mode ? "Manual" : "EC automatic";
}

int fan_count_or_default(hy_t *h)
{
    uint8_t n;

    if (hy_read(h, REG_FAN_COUNT, &n))
        return 0;
    return (n >= 1 && n <= MAX_FANS) ? n : 0;
}

int apply_duty(hy_t *h, int index, int duty, int order)
{
    if (hy_select(h, index))
        return -1;
    if (order == ORDER_MODE_FIRST) {
        if (hy_write(h, REG_TEST_MODE, 1))
            return -1;
        if (hy_select(h, index))
            return -1;
        if (hy_write(h, REG_PWM_DUTY, (uint8_t)duty))
            return -1;
    } else {
        if (hy_write(h, REG_PWM_DUTY, (uint8_t)duty))
            return -1;
        if (hy_write(h, REG_TEST_MODE, 1))
            return -1;
    }
    return 0;
}

int readback(hy_t *h, int index, int *duty, int *mode)
{
    uint8_t d, m;

    if (hy_select(h, index) || hy_read(h, REG_PWM_DUTY, &d) ||
        hy_read(h, REG_TEST_MODE, &m))
        return -1;
    *duty = d;
    *mode = m;
    return 0;
}

void hand_back(hy_t *h, int count)
{
    int idx;

    for (idx = 0; idx < count; idx++) {
        if (hy_select(h, idx))
            continue;
        hy_write(h, REG_TEST_MODE, 0);
    }
}

int still_manual(hy_t *h, int count, int *left)
{
    int idx, nleft = 0;

    for (idx = 0; idx < count; idx++) {
        int rb, md;
        if (readback(h, idx, &rb, &md) == 0 && md)
            left[nleft++] = idx;
    }
    return nleft;
}

int release_to_ec(hy_t *h, int count)
{
    int left[MAX_FANS];

    hand_back(h, count);
    if (!still_manual(h, count, left))
        return APPLY_CLEAN;
    hand_back(h, count);
    return still_manual(h, count, left) ? APPLY_FAILED : APPLY_RETRIED;
}
