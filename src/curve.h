#ifndef AFC_CURVE_H
#define AFC_CURVE_H

#include "common.h"

typedef struct {
    int temp;
    int percent;
} point_t;

int cmd_curve(args_t *a);

#endif
