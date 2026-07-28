#ifndef AFC_HEALTHY_H
#define AFC_HEALTHY_H

#include "ec.h"

#include <stdint.h>

typedef struct {
    ec_t *ec;
    double gap;
} hy_t;

int hy_version(hy_t *h, uint8_t *out);
int hy_read(hy_t *h, uint8_t reg, uint8_t *out);
int hy_write(hy_t *h, uint8_t reg, uint8_t value);
int hy_select(hy_t *h, int index);
int hy_rpm(hy_t *h, int *out);

#endif
