#ifndef AFC_MMIO_H
#define AFC_MMIO_H

typedef struct {
    int fd;
    volatile unsigned char *map;
    unsigned long page;
    unsigned long off;
} mmio_t;

void mmio_open(mmio_t *m, unsigned long base);
void mmio_close(mmio_t *m);
int mmio_be16(mmio_t *m, unsigned long offset);
int mmio_at(mmio_t *m, unsigned long address);
void mmio_fans(mmio_t *m, int fans[2]);
void mmio_temps(mmio_t *m, int *cpu, int *board);
const char *sensor_name(int sensor);
int sensor_pick(int sensor, int cpu, int board);

#endif
