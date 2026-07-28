#ifndef AFC_EC_H
#define AFC_EC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    bool direct;
    unsigned cmd_port;
    unsigned data_port;
    bool verbose;
} ec_t;

extern char ec_error[64];

int ec_open(ec_t *e, unsigned cmd_port, unsigned data_port, bool verbose);
void ec_close(ec_t *e);
int ec_xact(ec_t *e, uint8_t cmd, const uint8_t *payload, size_t n,
            bool want_result, uint8_t *out);

#endif
