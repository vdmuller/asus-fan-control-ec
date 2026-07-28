#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include "mmio.h"

#include "common.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

void mmio_open(mmio_t *m, unsigned long base)
{
    void *p;

    m->fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (m->fd < 0)
        die("Cannot open /dev/mem: %s.", strerror(errno));
    m->page = base & PAGE_MASK;
    m->off = base - m->page;
    p = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, m->fd, (off_t)m->page);
    if (p == MAP_FAILED)
        die("Cannot map 0x%lX: %s.", m->page, strerror(errno));
    m->map = p;
}

void mmio_close(mmio_t *m)
{
    if (m->map)
        munmap((void *)m->map, 0x1000);
    if (m->fd >= 0)
        close(m->fd);
    m->map = NULL;
    m->fd = -1;
}

int mmio_be16(mmio_t *m, unsigned long offset)
{
    unsigned long i = m->off + offset;

    return (m->map[i] << 8) | m->map[i + 1];
}

void mmio_fans(mmio_t *m, int fans[2])
{
    fans[0] = mmio_be16(m, TACH_OFFSET);
    fans[1] = mmio_be16(m, TACH_OFFSET + 2);
}

int mmio_at(mmio_t *m, unsigned long address)
{
    return m->map[address - m->page];
}

void mmio_temps(mmio_t *m, int *cpu, int *board)
{
    *cpu = mmio_at(m, CTMP);
    *board = mmio_at(m, CLOT);
}

const char *sensor_name(int sensor)
{
    switch (sensor) {
    case SENSOR_CPU:
        return "cpu";
    case SENSOR_BOARD:
        return "board";
    default:
        return "max";
    }
}

int sensor_pick(int sensor, int cpu, int board)
{
    switch (sensor) {
    case SENSOR_CPU:
        return cpu;
    case SENSOR_BOARD:
        return board;
    default:
        return imax(cpu, board);
    }
}
