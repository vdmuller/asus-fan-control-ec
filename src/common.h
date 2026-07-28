#ifndef AFC_COMMON_H
#define AFC_COMMON_H

#include <stdbool.h>

#define DATA_PORT       0x25C
#define CMD_PORT        0x25D
#define PREAMBLE        0xFF
#define CMD_VERSION     0xBB
#define CMD_TABLE       0xDD
#define TBL_READ        0x02
#define TBL_WRITE       0x82
#define REG_FAN_COUNT   0x30
#define REG_TEST_MODE   0x31
#define REG_FAN_INDEX   0x32
#define REG_RPM_LO      0x33
#define REG_RPM_HI      0x34
#define REG_PWM_DUTY    0x35
#define ERM2            0xFEDD8B00UL
#define ERAM            0xFEDD8300UL
#define PAGE_MASK       (~0xFFFUL)
#define CTMP            (ERAM + 0x58)
#define CLOT            (ERAM + 0x01)
#define ECMJ            (ERAM + 0xE4)
#define ECMN            (ERAM + 0xE5)
#define TACH_OFFSET     0x7C
#define APP_NAME        "asus-fan-control-ec"
#define APP_VERSION     "v1.0.0"
#define APP_URL         "https://github.com/Keyitdev/asus-fan-control-ec"
#define APP_KOFI        "https://ko-fi.com/keyitdev"
#define APP_LICENSE     "GPLv3+"
#define APP_COPYRIGHT   "Copyright (C) 2026 Keyitdev."
#define DEFAULT_CONFIG  "/etc/asus-fan-curve.conf"
#define PANIC_TEMP      96
#define HYSTERESIS      3
#define INTERVAL        3.0
#define OBF             0x01
#define IBF             0x02
#define POLL_LIMIT      1000
#define POLL_DELAY      0.0001
#define RETRIES         2
#define IO_GAP          0.02
#define TOL_PCT         5
#define TOL_MIN         3
#define READ_RETRIES    3
#define TACH_TOL        64
#define RELEASE_ARG     (-1)
#define CURVE_UNSET     (-2)
#define MAX_FANS        2
#define MAX_POINTS      64

enum { ORDER_MODE_FIRST, ORDER_DUTY_FIRST };
enum { SENSOR_CPU, SENSOR_BOARD, SENSOR_MAX };
enum { APPLY_FAILED, APPLY_CLEAN, APPLY_RETRIED };

typedef struct {
    unsigned cmd_port;
    unsigned data_port;
    double gap;
    bool verbose;
    int index;
    int retries;
    bool no_verify;
    int order;
    int duty;
    int percent;
    const char *config;
    double interval;
    int hysteresis;
    int panic_temp;
    int sensor;
    bool once;
    bool dry_run;
    bool silent;
} args_t;

#endif
