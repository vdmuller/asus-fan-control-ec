#ifndef AFC_COMMANDS_H
#define AFC_COMMANDS_H

#include "common.h"

int cmd_version(args_t *a);
int cmd_fan_info(args_t *a);
int cmd_fan_speed(args_t *a);
int cmd_temps_info(args_t *a);
int cmd_set(args_t *a);
int cmd_setp(args_t *a);

#endif
