#ifndef AFC_FAN_H
#define AFC_FAN_H

#include "healthy.h"

#include <stdbool.h>

int percent_to_duty(int percent);
int duty_to_percent(int duty);
bool duty_close(int readback_value, int want);
const char *mode_name(int mode);
int fan_count_or_default(hy_t *h);
int apply_duty(hy_t *h, int index, int duty, int order);
int readback(hy_t *h, int index, int *duty, int *mode);
void hand_back(hy_t *h, int count);
int still_manual(hy_t *h, int count, int *left);
int release_to_ec(hy_t *h, int count);

#endif
