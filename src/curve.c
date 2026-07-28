#define _GNU_SOURCE

#include "curve.h"

#include "ec.h"
#include "fan.h"
#include "healthy.h"
#include "mmio.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stop_flag = 0;

static int point_cmp(const void *a, const void *b)
{
    const point_t *p = a, *q = b;

    if (p->temp != q->temp)
        return p->temp < q->temp ? -1 : 1;
    if (p->percent != q->percent)
        return p->percent < q->percent ? -1 : 1;
    return 0;
}

static int load_curve(const char *path, point_t *points)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int n = 0, num = 0, i;

    if (!f)
        die("Config not found: %s.", path);
    while (fgets(line, sizeof line, f)) {
        char *hash, *sep, *end, *rest;
        long percent, temp;

        num++;
        hash = strchr(line, '#');
        if (hash)
            *hash = '\0';
        rest = line;
        while (*rest && isspace((unsigned char)*rest))
            rest++;
        end = rest + strlen(rest);
        while (end > rest && isspace((unsigned char)end[-1]))
            *--end = '\0';
        if (!*rest)
            continue;
        sep = strpbrk(rest, ",;");
        if (!sep || strpbrk(sep + 1, ",;")) {
            fclose(f);
            die("%s:%d: expected 'percent,temp'.", path, num);
        }
        *sep = '\0';
        errno = 0;
        percent = strtol(rest, &end, 10);
        while (end && *end && isspace((unsigned char)*end))
            end++;
        if (errno || end == rest || *end) {
            fclose(f);
            die("%s:%d: not a pair of integers.", path, num);
        }
        rest = sep + 1;
        while (*rest && isspace((unsigned char)*rest))
            rest++;
        errno = 0;
        temp = strtol(rest, &end, 10);
        while (end && *end && isspace((unsigned char)*end))
            end++;
        if (errno || end == rest || *end) {
            fclose(f);
            die("%s:%d not a pair of integers", path, num);
        }
        if ((percent < 0 && percent != RELEASE_ARG) || percent > 100) {
            fclose(f);
            die("%s:%d: percent must be 0..100, or -1 for EC control.", path, num);
        }
        if (temp < 0 || temp > 120) {
            fclose(f);
            die("%s:%d: temperature out of range.", path, num);
        }
        if (n == MAX_POINTS) {
            fclose(f);
            die("%s has too many points.", path);
        }
        points[n].temp = (int)temp;
        points[n].percent = (int)percent;
        n++;
    }
    fclose(f);
    if (!n)
        die("%s has no usable lines.", path);
    qsort(points, (size_t)n, sizeof points[0], point_cmp);
    for (i = 1; i < n; i++)
        if (points[i].temp == points[i - 1].temp)
            die("%s has duplicate temperatures.", path);
    return n;
}

static const char *percent_text(char *buf, size_t size, int percent)
{
    if (percent == RELEASE_ARG)
        snprintf(buf, size, "EC (auto)");
    else
        snprintf(buf, size, "%d%%", percent);
    return buf;
}

static void describe_curve(const point_t *points, int n)
{
    char text[16];
    int i;

    if (points[0].temp > 0)
        printf("  below %d C -> 0%%\n", points[0].temp);
    for (i = 0; i < n; i++) {
        percent_text(text, sizeof text, points[i].percent);
        if (i + 1 < n)
            printf("  %d-%d C -> %s\n", points[i].temp, points[i + 1].temp, text);
        else
            printf("  %d C and above -> %s\n", points[i].temp, text);
    }
}

static int curve_band(const point_t *points, int n, int temp, int current,
                      int hysteresis)
{
    int band = -1, keep = -1, i;

    for (i = 0; i < n; i++) {
        if (temp >= points[i].temp)
            band = i;
        if (temp + hysteresis >= points[i].temp)
            keep = i;
    }
    if (current == CURVE_UNSET || band >= current)
        return band;
    return imax(band, imin(current, keep));
}

static int band_percent(const point_t *points, int band)
{
    return band < 0 ? 0 : points[band].percent;
}

static int apply_percent(hy_t *h, int count, int percent, args_t *a)
{
    int duty = percent_to_duty(percent);
    int marks[MAX_FANS];
    int attempt, r, idx;
    bool retried = false;

    for (attempt = 1; attempt <= a->retries; attempt++) {
        bool all_seen = false, agreed = true;

        for (idx = 0; idx < count; idx++)
            apply_duty(h, idx, duty, a->order);
        for (r = 1; r <= READ_RETRIES; r++) {
            all_seen = true;
            for (idx = 0; idx < count; idx++) {
                int rb, md;
                marks[idx] = readback(h, idx, &rb, &md) ? -1 : rb;
                if (marks[idx] < 0)
                    all_seen = false;
            }
            if (all_seen)
                break;
            retried = true;
            if (r < READ_RETRIES)
                printf("  Readback failed, retrying read %d/%d.\n", r + 1,
                       READ_RETRIES);
        }
        if (all_seen) {
            for (idx = 1; idx < count; idx++)
                if (marks[idx] != marks[0])
                    agreed = false;
            if (agreed && duty_close(marks[0], duty))
                return retried ? APPLY_RETRIED : APPLY_CLEAN;
        }
        if (attempt < a->retries) {
            retried = true;
            printf("  Setting not confirmed, retrying write %d/%d.\n",
                   attempt + 1, a->retries);
        }
    }
    return APPLY_FAILED;
}

static void on_stop(int sig)
{
    (void)sig;
    stop_flag = 1;
}

int cmd_curve(args_t *a)
{
    point_t points[MAX_POINTS];
    ec_t ec;
    hy_t h;
    mmio_t mm;
    int n, count, current = CURVE_UNSET, band = CURVE_UNSET;
    struct sigaction sa;

    n = load_curve(a->config, points);
    printf("Curve loaded from %s.\n", a->config);
    describe_curve(points, n);
    printf("sensor=%s interval=%.1fs hysteresis=%dC panic=%dC\n",
           sensor_name(a->sensor),
           a->interval, a->hysteresis, a->panic_temp);
    if (a->dry_run)
        return 0;

    ec_open(&ec, a->cmd_port, a->data_port, a->verbose);
    h.ec = &ec;
    h.gap = a->gap;
    mmio_open(&mm, ERM2);
    count = fan_count_or_default(&h);
    if (count < 1 || count > MAX_FANS) {
        mmio_close(&mm);
        ec_close(&ec);
        die("Fan count from 0x30 is implausible, refusing to run.");
    }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    for (;;) {
        int cpu, board, temp, want, next, fans[2], ticks, i;
        bool panic;
        const char *status, *mode;
        char clock[16], target[8];
        time_t now;

        mmio_temps(&mm, &cpu, &board);
        temp = sensor_pick(a->sensor, cpu, board);
        next = curve_band(points, n, temp, band, a->hysteresis);
        want = band_percent(points, next);
        panic = temp >= a->panic_temp;
        if (panic)
            want = 100;
        if (want != current) {
            int applied = want == RELEASE_ARG ? release_to_ec(&h, count)
                                             : apply_percent(&h, count, want, a);
            if (applied != APPLY_FAILED) {
                current = want;
                band = next;
            }
            status = applied == APPLY_FAILED ? "FAILED"
                     : applied == APPLY_RETRIED ? "retried" : "applied";
        } else {
            band = next;
            status = "hold";
        }
        mode = current == RELEASE_ARG ? "EC (auto)"
               : current == CURVE_UNSET ? "unknown" : "manual";
        if (want == RELEASE_ARG)
            snprintf(target, sizeof target, "%4s", "--");
        else
            snprintf(target, sizeof target, "%3d%%", want);
        mmio_fans(&mm, fans);
        now = time(NULL);
        strftime(clock, sizeof clock, "%H:%M:%S", localtime(&now));
        if (!a->silent) {
            printf("%s temp=%dC target=%s %-9s %-7s fan0=%5d fan1=%5d%s\n", clock,
                   temp, target, mode, status, fans[0], fans[1],
                   panic ? "  PANIC" : "");
            fflush(stdout);
        }
        if (a->once || stop_flag)
            break;
        ticks = (int)(a->interval * 10);
        for (i = 0; i < ticks && !stop_flag; i++)
            nsleep(0.1);
        if (stop_flag)
            break;
    }

    if (!a->once) {
        hand_back(&h, count);
        printf("Handed fan control back to the EC.\n");
    }
    mmio_close(&mm);
    ec_close(&ec);
    return 0;
}
