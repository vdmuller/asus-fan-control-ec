#define _GNU_SOURCE

#include "commands.h"

#include "common.h"
#include "ec.h"
#include "fan.h"
#include "healthy.h"
#include "mmio.h"
#include "util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_model(void)
{
    char vendor[128], product[128];

    if (read_line_file("/sys/class/dmi/id/sys_vendor", vendor, sizeof vendor) &&
        read_line_file("/sys/class/dmi/id/product_name", product, sizeof product))
        printf("Laptop model: %s %s (DMI)\n", vendor, product);
    else if (read_line_file("/sys/class/dmi/id/product_name", product,
                            sizeof product))
        printf("Laptop model: %s (DMI)\n", product);
    else
        printf("Laptop model: unreadable (/sys/class/dmi/id)\n");
}

static void print_identity(hy_t *h, mmio_t *m)
{
    uint8_t version;
    int mj, mn;

    print_model();
    mj = mmio_at(m, ECMJ);
    mn = mmio_at(m, ECMN);
    printf("Embedded controller version: %d.%d (0x%08lX = %02X %02X)\n", mj, mn,
           ECMJ, (unsigned)mj, (unsigned)mn);

    if (hy_version(h, &version))
        printf("Healthy table version: unreadable (0xBB 50)\n");
    else
        printf("Healthy table version: %d (0xBB 50)\n", version);
}

static void print_project(void)
{
    printf("\n");
    printf("%s version: %s\n", APP_NAME, APP_VERSION);
    printf("Source code: %s\n", APP_URL);
    printf("Support the project: %s\n", APP_KOFI);
    printf("License: %s\n", APP_LICENSE);
    printf("%s\n", APP_COPYRIGHT);
}

int cmd_version(args_t *a)
{
    ec_t ec;
    hy_t h;
    mmio_t mm;

    if (geteuid() == 0) {
        ec_open(&ec, a->cmd_port, a->data_port, a->verbose);
        h.ec = &ec;
        h.gap = a->gap;
        mmio_open(&mm, ERM2);
        print_identity(&h, &mm);
        mmio_close(&mm);
        ec_close(&ec);
    } else {
        print_model();
        printf("Embedded controller version: root required\n");
        printf("Healthy table version: root required\n");
    }
    print_project();
    return 0;
}

int cmd_fan_info(args_t *a)
{
    ec_t ec;
    hy_t h;
    mmio_t mm;
    int duty[MAX_FANS], mode[MAX_FANS], rpm[MAX_FANS];
    int hi[MAX_FANS], lo[MAX_FANS], ref[MAX_FANS];
    int count, idx, agree = 0, checked = 0, spinning = 0, rc = 0;
    bool uniform = true;

    ec_open(&ec, a->cmd_port, a->data_port, a->verbose);
    h.ec = &ec;
    h.gap = a->gap;
    mmio_open(&mm, ERM2);

    count = fan_count_or_default(&h);
    if (count < 1 || count > MAX_FANS) {
        printf("Fan count: unreadable (0x30 out of range)\n");
        rc = 1;
        goto out;
    }
    printf("Fan count: %d (0x30=%d)\n", count, count);

    for (idx = 0; idx < count; idx++) {
        uint8_t d = 0, m = 0, rh = 0, rl = 0;

        duty[idx] = -1;
        mode[idx] = -1;
        rpm[idx] = -1;
        ref[idx] = -1;
        hi[idx] = 0;
        lo[idx] = 0;
        if (hy_select(&h, idx))
            continue;
        if (!hy_read(&h, REG_PWM_DUTY, &d))
            duty[idx] = d;
        if (!hy_read(&h, REG_TEST_MODE, &m))
            mode[idx] = m;
        if (!hy_read(&h, REG_RPM_HI, &rh) && !hy_read(&h, REG_RPM_LO, &rl)) {
            hi[idx] = rh;
            lo[idx] = rl;
            rpm[idx] = (rh << 8) | rl;
        }
        if (idx < 2)
            ref[idx] = mmio_be16(&mm, TACH_OFFSET + 2 * (unsigned long)idx);
        if (ref[idx] > 0)
            spinning++;
    }

    for (idx = 1; idx < count; idx++)
        if (mode[idx] != mode[0])
            uniform = false;
    if (!uniform)
        printf("Fan control: differs between fans (see below)\n");
    else if (mode[0] < 0)
        printf("Fan control: unreadable (0x31)\n");
    else
        printf("Fan control: %s (0x31=%d)\n", mode_name(mode[0]), mode[0]);

    for (idx = 0; idx < count; idx++) {
        unsigned long addr = ERM2 + TACH_OFFSET + 2 * (unsigned long)idx;
        int expect = ref[idx];

        printf("\nfan%d:\n", idx);
        if (duty[idx] < 0)
            printf("  PWM duty:       unreadable (0x35)\n");
        else
            printf("  PWM duty:       %d (%d%%) (0x35=%d)\n", duty[idx],
                   duty_to_percent(duty[idx]), duty[idx]);
        if (!uniform)
            printf("  Fan control:    %s (0x31=%d)\n", mode_name(mode[idx]),
                   mode[idx]);
        if (rpm[idx] < 0)
            printf("  Fan speed reg:  unreadable (0x34/0x33)\n");
        else
            printf("  Fan speed reg:  %d rpm (0x34/0x33 = %02X %02X)\n", rpm[idx],
                   (unsigned)hi[idx], (unsigned)lo[idx]);
        if (expect < 0)
            printf("  Fan speed mmio: no aperture slot\n");
        else
            printf("  Fan speed mmio: %d rpm (0x%08lX)\n", expect, addr);
        if (rpm[idx] < 0 || expect < 0) {
            printf("  Verdict:        UNKNOWN\n");
            continue;
        }
        checked++;
        if (abs(rpm[idx] - expect) <= TACH_TOL) {
            agree++;
            printf("  Verdict:        MATCH\n");
        } else {
            printf("  Verdict:        MISMATCH\n");
        }
    }

    printf("\n");
    if (!spinning) {
        printf("Result: both tachometers read zero, spin the fans up and retry.\n");
        rc = 1;
    } else if (checked == count && agree == count) {
        printf("Result: VALIDATED against the MMIO aperture.\n");
        printf("If the program works on your device and it is not listed\nin the tested devices section on github, please open an issue.\n");
    } else {
        printf("Result: NOT VALIDATED, do not write anything.\n");
        rc = 1;
    }

out:
    mmio_close(&mm);
    ec_close(&ec);
    return rc;
}

int cmd_temps_info(args_t *a)
{
    mmio_t mm;
    int cpu, board;

    (void)a;
    mmio_open(&mm, ERAM);
    cpu = mmio_at(&mm, CTMP);
    board = mmio_at(&mm, CLOT);
    mmio_close(&mm);

    printf("CPU:         %d C (0x%08lX = %02X, ERAM+0x58 CTMP)\n", cpu, CTMP,
           (unsigned)cpu);
    printf("Board:       %d C (0x%08lX = %02X, ERAM+0x01 CLOT)\n", board, CLOT,
           (unsigned)board);
    printf("Max:         %d C (higher of the two)\n", imax(cpu, board));
    return 0;
}

int cmd_fan_speed(args_t *a)
{
    ec_t ec;
    hy_t h;
    uint8_t mode;
    int n, idx;

    ec_open(&ec, a->cmd_port, a->data_port, a->verbose);
    h.ec = &ec;
    h.gap = a->gap;
    n = fan_count_or_default(&h);
    if (n < 1 || n > MAX_FANS)
        n = 2;
    if (hy_read(&h, REG_TEST_MODE, &mode))
        printf("Fan control: unreadable (0x31)\n");
    else
        printf("Fan control: %s (0x31=%d)\n", mode_name(mode), mode);
    for (idx = 0; idx < n; idx++) {
        int rpm;
        if (hy_select(&h, idx) || hy_rpm(&h, &rpm))
            printf("fan %d: speed unreadable\n", idx);
        else
            printf("fan %d: %d rpm\n", idx, rpm);
    }
    ec_close(&ec);
    return 0;
}

static int verify(hy_t *h, int duty, const int *pending, int npending,
                  int *failed)
{
    int i, nfailed = 0;

    for (i = 0; i < npending; i++) {
        int idx = pending[i], rb, md;
        bool ok;

        printf("fan %d:\n", idx);
        if (readback(h, idx, &rb, &md)) {
            printf("  Set:      FAILED, readback error: %s\n", ec_error);
            printf("  PWM duty: unreadable (0x35)\n");
            ok = false;
        } else {
            ok = duty_close(rb, duty);
            if (ok)
                printf("  Set:      OK\n");
            else
                printf("  Set:      FAILED, wanted duty %d\n", duty);
            printf("  PWM duty: %d (%d%%) (0x35=%d)\n", rb, duty_to_percent(rb),
                   rb);
        }
        if (!ok)
            failed[nfailed++] = idx;
    }
    return nfailed;
}

static int drive(hy_t *h, int duty, const int *targets, int ntargets,
                 args_t *a, int *left)
{
    int pending[MAX_FANS], npending = ntargets;
    int failed[MAX_FANS], nfailed;
    int attempt, i;
    char list[64];

    memcpy(pending, targets, sizeof(int) * (size_t)ntargets);
    for (attempt = 1; attempt <= a->retries; attempt++) {
        join_ints(list, sizeof list, pending, npending);
        printf("Attempt %d/%d, fans %s, duty %d.\n", attempt, a->retries, list, duty);
        for (i = 0; i < npending; i++)
            if (apply_duty(h, pending[i], duty, a->order))
                printf("  fan %d FAIL, write failed: %s.\n", pending[i], ec_error);
        if (a->no_verify)
            return 0;
        nfailed = verify(h, duty, pending, npending, failed);
        if (!nfailed)
            return 0;
        memcpy(pending, failed, sizeof(int) * (size_t)nfailed);
        npending = nfailed;
    }
    memcpy(left, pending, sizeof(int) * (size_t)npending);
    return npending;
}

static int run_set(args_t *a, int duty)
{
    ec_t ec;
    hy_t h;
    mmio_t mm;
    int count, targets[MAX_FANS], ntargets, left[MAX_FANS], nleft, fans[2], i;
    char list[64];

    if (duty < 0 || duty > 255)
        die("Duty must be 0..255, or -1 to hand control back to the EC.");
    ec_open(&ec, a->cmd_port, a->data_port, a->verbose);
    h.ec = &ec;
    h.gap = a->gap;
    mmio_open(&mm, ERM2);

    count = fan_count_or_default(&h);
    if (count < 1 || count > MAX_FANS) {
        mmio_close(&mm);
        ec_close(&ec);
        die("Fan count from 0x30 is implausible, refusing to write.");
    }
    mmio_fans(&mm, fans);
    printf("Fan count: %d, speeds before: %d/%d rpm.\n", count, fans[0], fans[1]);
    if (a->index < 0) {
        ntargets = count;
        for (i = 0; i < count; i++)
            targets[i] = i;
    } else {
        ntargets = 1;
        targets[0] = a->index;
    }
    nleft = drive(&h, duty, targets, ntargets, a, left);
    mmio_fans(&mm, fans);
    printf("Fan count: %d, speeds after: %d/%d rpm.\n", count, fans[0], fans[1]);
    mmio_close(&mm);
    ec_close(&ec);
    if (nleft) {
        join_ints(list, sizeof list, left, nleft);
        printf("\n");
        printf("Fans %s did not take the setting after %d attempts.\n", list,
               a->retries);
        return 1;
    }
    return 0;
}

static int run_release(args_t *a)
{
    ec_t ec;
    hy_t h;
    int left[MAX_FANS];
    int n, nleft;
    char list[64];

    ec_open(&ec, a->cmd_port, a->data_port, a->verbose);
    h.ec = &ec;
    h.gap = a->gap;

    n = fan_count_or_default(&h);
    if (n < 1 || n > MAX_FANS)
        n = 2;
    hand_back(&h, n);
    nleft = still_manual(&h, n, left);
    if (nleft) {
        join_ints(list, sizeof list, left, nleft);
        printf("Fans %s still in manual, retrying.\n", list);
        hand_back(&h, n);
        nleft = still_manual(&h, n, left);
    }
    ec_close(&ec);
    if (nleft) {
        join_ints(list, sizeof list, left, nleft);
        printf("Fans %s are still in manual, the EC did not take the release.\n",
               list);
        return 1;
    }
    printf("Handed fan control back to the EC.\n");
    return 0;
}

int cmd_set(args_t *a)
{
    if (a->duty == RELEASE_ARG)
        return run_release(a);
    return run_set(a, a->duty);
}

int cmd_setp(args_t *a)
{
    int duty;

    if (a->percent == RELEASE_ARG)
        return run_release(a);
    if (a->percent < 0 || a->percent > 100)
        die("Percent must be 0..100, or -1 to hand control back to the EC.");
    duty = percent_to_duty(a->percent);
    printf("%d%% maps to duty %d.\n", a->percent, duty);
    return run_set(a, duty);
}
