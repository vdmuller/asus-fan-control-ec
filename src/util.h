#ifndef AFC_UTIL_H
#define AFC_UTIL_H

#include <stdbool.h>
#include <stddef.h>

void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void nsleep(double seconds);
bool read_line_file(const char *path, char *buf, size_t size);
void join_ints(char *buf, size_t size, const int *v, int n);

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

#endif
