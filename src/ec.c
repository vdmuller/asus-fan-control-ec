#define _GNU_SOURCE

#include "ec.h"

#include "common.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__i386__) || defined(__x86_64__)
#include <sys/io.h>
#define HAVE_PORT_IO 1
#else
#define HAVE_PORT_IO 0
#endif

char ec_error[64] = "";

int ec_open(ec_t *e, unsigned cmd_port, unsigned data_port, bool verbose)
{
    e->cmd_port = cmd_port;
    e->data_port = data_port;
    e->verbose = verbose;
    e->fd = -1;
    e->direct = false;
#if HAVE_PORT_IO
    if (cmd_port < 0x400 && data_port < 0x400 &&
        ioperm(cmd_port, 1, 1) == 0 && ioperm(data_port, 1, 1) == 0) {
        e->direct = true;
        return 0;
    }
#endif
    e->fd = open("/dev/port", O_RDWR);
    if (e->fd < 0)
        die("Cannot open /dev/port: %s.", strerror(errno));
    return 0;
}

void ec_close(ec_t *e)
{
    if (e->fd >= 0)
        close(e->fd);
    e->fd = -1;
}

static uint8_t ec_in8(ec_t *e, unsigned port)
{
    uint8_t v;

#if HAVE_PORT_IO
    if (e->direct)
        return inb((unsigned short)port);
#endif
    if (pread(e->fd, &v, 1, (off_t)port) != 1)
        die("Cannot read port 0x%03X: %s.", port, strerror(errno));
    return v;
}

static void ec_out8(ec_t *e, unsigned port, uint8_t val)
{
    if (e->verbose)
        printf("      out 0x%03X <- %02X\n", port, (unsigned)val);
#if HAVE_PORT_IO
    if (e->direct) {
        outb(val, (unsigned short)port);
        return;
    }
#endif
    if (pwrite(e->fd, &val, 1, (off_t)port) != 1)
        die("Cannot write port 0x%03X: %s.", port, strerror(errno));
}

static uint8_t ec_status(ec_t *e)
{
    return ec_in8(e, e->cmd_port);
}

static int ec_timeout(const char *what)
{
    snprintf(ec_error, sizeof ec_error, "%s", what);
    return -1;
}

static int ec_drain(ec_t *e)
{
    int i;

    for (i = 0; i < POLL_LIMIT; i++) {
        if (!(ec_status(e) & OBF))
            return 0;
        ec_in8(e, e->data_port);
        nsleep(POLL_DELAY);
    }
    return ec_timeout("obf never cleared");
}

static int ec_wait_ibf(ec_t *e)
{
    int i;

    for (i = 0; i < POLL_LIMIT; i++) {
        if (!(ec_status(e) & IBF))
            return 0;
        nsleep(POLL_DELAY);
    }
    return ec_timeout("ibf never cleared");
}

static int ec_wait_obf(ec_t *e)
{
    int i;

    for (i = 0; i < POLL_LIMIT; i++) {
        if (ec_status(e) & OBF)
            return 0;
        nsleep(POLL_DELAY);
    }
    return ec_timeout("obf never set");
}

static int ec_once(ec_t *e, uint8_t cmd, const uint8_t *payload, size_t n,
                   bool want_result, uint8_t *out)
{
    size_t i;

    if (ec_drain(e) || ec_wait_ibf(e))
        return -1;
    ec_out8(e, e->cmd_port, PREAMBLE);
    if (ec_wait_ibf(e))
        return -1;
    ec_out8(e, e->cmd_port, cmd);
    for (i = 0; i < n; i++) {
        if (ec_wait_ibf(e))
            return -1;
        ec_out8(e, e->data_port, payload[i]);
    }
    if (ec_wait_ibf(e))
        return -1;
    if (!want_result)
        return 0;
    if (ec_wait_obf(e))
        return -1;
    if (out)
        *out = ec_in8(e, e->data_port);
    return 0;
}

int ec_xact(ec_t *e, uint8_t cmd, const uint8_t *payload, size_t n,
                   bool want_result, uint8_t *out)
{
    int i;

    if (n > 8)
        die("Payload limit is 8 bytes.");
    for (i = 0; i < RETRIES; i++)
        if (ec_once(e, cmd, payload, n, want_result, out) == 0)
            return 0;
    return -1;
}
