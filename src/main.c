#define _GNU_SOURCE

#include "commands.h"
#include "common.h"
#include "curve.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void need_root(void)
{
    if (geteuid() != 0)
        die("Root privileges required.");
}

static const char INT_MSG[] =
    "\nInterrupted. Fans keep the last setting, use 'set -1' to hand control "
    "back to the EC.\n";

static void on_interrupt(int sig)
{
    (void)sig;
    if (write(STDOUT_FILENO, INT_MSG, sizeof INT_MSG - 1) < 0) {
    }
    _exit(130);
}

static void usage_text(FILE *out)
{
    fprintf(out,
        "usage: " APP_NAME " [global options] <command> [options]\n"
        "\n"
        "global options:\n"
        "  --cmd-port N   --data-port N   --gap SECONDS   --verbose\n"
        "\n"
        "commands:\n"
        "  set        DUTY [--fan N] [--retries N] [--no-verify]\n"
        "             [--order mode-first|duty-first]\n"
        "             duty 0-255, or -1 to hand control back to the EC\n"
        "             -1 always covers every fan, 0x31 is global\n"
        "  setp       PERCENT [same options as set], or -1 for the same release\n"
        "  curve      [--config PATH] [--interval S] [--hysteresis C]\n"
        "             [--panic-temp C] [--sensor cpu|board|max] [--retries N]\n"
        "             [--order ORDER] [--once] [--dry-run] [--silent]\n"
        "             config lines are 'percent,temp'; percent -1 hands that\n"
        "             band back to the EC\n"
        "  fan-speed\n"
        "  fan-info\n"
        "  temps-info\n"
        "  help\n"
        "  version\n");
}

static void usage(void)
{
    usage_text(stderr);
    exit(2);
}

static int cmd_help(void)
{
    usage_text(stdout);
    return 0;
}

static const char *need_value(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc)
        die("%s requires a value.", argv[*i]);
    return argv[++(*i)];
}

static long int_auto(const char *s, const char *what)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 0);
    if (errno || end == s || *end)
        die("Invalid %s: %s.", what, s);
    return v;
}

static double dbl_arg(const char *s, const char *what)
{
    char *end;
    double v;

    errno = 0;
    v = strtod(s, &end);
    if (errno || end == s || *end)
        die("invalid %s: %s", what, s);
    return v;
}

static int pick(const char *value, const char *what, const char *const *names,
                int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(value, names[i]) == 0)
            return i;
    die("Invalid %s: %s.", what, value);
}

static bool global_option(args_t *a, int argc, char **argv, int *i)
{
    const char *arg = argv[*i];

    if (strcmp(arg, "--cmd-port") == 0)
        a->cmd_port = (unsigned)int_auto(need_value(argc, argv, i), "--cmd-port");
    else if (strcmp(arg, "--data-port") == 0)
        a->data_port = (unsigned)int_auto(need_value(argc, argv, i), "--data-port");
    else if (strcmp(arg, "--gap") == 0)
        a->gap = dbl_arg(need_value(argc, argv, i), "--gap");
    else if (strcmp(arg, "--verbose") == 0)
        a->verbose = true;
    else
        return false;
    return true;
}

int main(int argc, char **argv)
{
    static const char *const orders[] = { "mode-first", "duty-first" };
    static const char *const sensors[] = { "cpu", "board", "max" };
    args_t a;
    struct sigaction sa;
    const char *cmd = NULL;
    bool have_duty = false;
    int i;

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "help") == 0 || strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0)
            return cmd_help();

    memset(&a, 0, sizeof a);
    a.cmd_port = CMD_PORT;
    a.data_port = DATA_PORT;
    a.gap = IO_GAP;
    a.index = -1;
    a.retries = 3;
    a.order = ORDER_MODE_FIRST;
    a.config = DEFAULT_CONFIG;
    a.interval = INTERVAL;
    a.hysteresis = HYSTERESIS;
    a.panic_temp = PANIC_TEMP;
    a.sensor = SENSOR_MAX;

    i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!global_option(&a, argc, argv, &i))
            usage();
        i++;
    }
    if (i >= argc)
        usage();
    cmd = argv[i++];
    if (strcmp(cmd, "version") != 0)
        need_root();

    for (; i < argc; i++) {
        const char *arg = argv[i];

        if (global_option(&a, argc, argv, &i))
            continue;
        if (arg[0] != '-' || isdigit((unsigned char)arg[1])) {
            if (strcmp(cmd, "set") == 0 && !have_duty) {
                a.duty = (int)int_auto(arg, "duty");
                have_duty = true;
            } else if (strcmp(cmd, "setp") == 0 && !have_duty) {
                a.percent = (int)int_auto(arg, "percent");
                have_duty = true;
            } else {
                usage();
            }
            continue;
        }
        if (strcmp(arg, "--fan") == 0)
            a.index = (int)int_auto(need_value(argc, argv, &i), "--fan");
        else if (strcmp(arg, "--retries") == 0)
            a.retries = (int)int_auto(need_value(argc, argv, &i), "--retries");
        else if (strcmp(arg, "--no-verify") == 0)
            a.no_verify = true;
        else if (strcmp(arg, "--order") == 0)
            a.order = pick(need_value(argc, argv, &i), "--order", orders, 2);
        else if (strcmp(arg, "--sensor") == 0)
            a.sensor = pick(need_value(argc, argv, &i), "--sensor", sensors, 3);
        else if (strcmp(arg, "--config") == 0)
            a.config = need_value(argc, argv, &i);
        else if (strcmp(arg, "--interval") == 0)
            a.interval = dbl_arg(need_value(argc, argv, &i), "--interval");
        else if (strcmp(arg, "--hysteresis") == 0)
            a.hysteresis = (int)int_auto(need_value(argc, argv, &i), "--hysteresis");
        else if (strcmp(arg, "--panic-temp") == 0)
            a.panic_temp = (int)int_auto(need_value(argc, argv, &i), "--panic-temp");
        else if (strcmp(arg, "--once") == 0)
            a.once = true;
        else if (strcmp(arg, "--dry-run") == 0)
            a.dry_run = true;
        else if (strcmp(arg, "--silent") == 0)
            a.silent = true;
        else {
            usage();
        }
    }

    if (strcmp(cmd, "set") == 0 && !have_duty)
        die("set requires a duty value.");
    if (strcmp(cmd, "setp") == 0 && !have_duty)
        die("setp requires a percent value.");
    if (a.index >= MAX_FANS)
        die("--fan must be 0..%d.", MAX_FANS - 1);
    if (a.retries < 1)
        die("--retries must be at least 1.");

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_interrupt;
    sigaction(SIGINT, &sa, NULL);

    if (strcmp(cmd, "version") == 0)
        return cmd_version(&a);
    if (strcmp(cmd, "fan-info") == 0)
        return cmd_fan_info(&a);
    if (strcmp(cmd, "temps-info") == 0)
        return cmd_temps_info(&a);
    if (strcmp(cmd, "fan-speed") == 0)
        return cmd_fan_speed(&a);
    if (strcmp(cmd, "set") == 0)
        return cmd_set(&a);
    if (strcmp(cmd, "setp") == 0)
        return cmd_setp(&a);
    if (strcmp(cmd, "curve") == 0)
        return cmd_curve(&a);
    usage();
    return 2;
}
