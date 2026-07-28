#define _GNU_SOURCE

#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void nsleep(double seconds)
{
    struct timespec ts;

    if (seconds <= 0.0)
        return;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    if (ts.tv_nsec < 0)
        ts.tv_nsec = 0;
    if (ts.tv_nsec > 999999999L)
        ts.tv_nsec = 999999999L;
    nanosleep(&ts, NULL);
}

bool read_line_file(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    size_t len;

    if (!f)
        return false;
    if (!fgets(buf, (int)size, f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    len = strlen(buf);
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return len > 0;
}

void join_ints(char *buf, size_t size, const int *v, int n)
{
    int i;
    size_t used = 0;

    buf[0] = '\0';
    for (i = 0; i < n && used + 8 < size; i++)
        used += (size_t)snprintf(buf + used, size - used, "%s%d", i ? " " : "", v[i]);
}
