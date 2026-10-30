#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

struct ev32 { uint32_t sec, usec; uint16_t type, code; int32_t value; };

#define EV_SYN              0x00
#define EV_KEY              0x01
#define EV_ABS              0x03
#define SYN_REPORT          0x00
#define BTN_TOUCH           0x14a
#define ABS_MT_SLOT         0x2f
#define ABS_MT_POSITION_X   0x35
#define ABS_MT_POSITION_Y   0x36
#define ABS_MT_TRACKING_ID  0x39

static int g_fd = -1;
static int g_err = 0;

static void emit(uint16_t type, uint16_t code, int32_t value)
{
    struct ev32 e;
    memset(&e, 0, sizeof e);
    e.type = type; e.code = code; e.value = value;
    if (write(g_fd, &e, sizeof e) != (ssize_t)sizeof e) { perror("write"); g_err = 1; }
}
static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }

static int fx(int x) { return 360 - x; }
static int fy(int y) { return 360 - y; }

static void usage(const char *p)
{
    fprintf(stderr, "usage: %s tap <x> <y> | hold <x> <y> <ms> | swipe <x1> <y1> <x2> <y2>\n", p);
}

static int parse_coord(const char *s)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < 0 || v > 359) {
        fprintf(stderr, "coordinate out of range (0..359): %s\n", s);
        exit(2);
    }
    return (int)v;
}

int main(int argc, char **argv)
{
    if (argc < 4) { usage(argv[0]); return 2; }

    g_fd = open("/dev/input/event1", O_WRONLY);
    if (g_fd < 0) { perror("open /dev/input/event1"); return 1; }

    if ((!strcmp(argv[1], "tap") && argc == 4) || (!strcmp(argv[1], "hold") && argc == 5)) {
        int hold_ms = !strcmp(argv[1], "hold") ? atoi(argv[4]) : 0;
        int x = fx(parse_coord(argv[2])), y = fy(parse_coord(argv[3]));
        emit(EV_ABS, ABS_MT_SLOT, 0);
        emit(EV_ABS, ABS_MT_TRACKING_ID, 1);
        emit(EV_ABS, ABS_MT_POSITION_X, x);
        emit(EV_ABS, ABS_MT_POSITION_Y, y);
        emit(EV_KEY, BTN_TOUCH, 1);
        syn();
        usleep(80000);
        for (int i = 0; i < 4 + hold_ms / 35; i++) {
            emit(EV_ABS, ABS_MT_POSITION_X, x);
            emit(EV_ABS, ABS_MT_POSITION_Y, y);
            syn();
            usleep(35000);
        }
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(EV_KEY, BTN_TOUCH, 0);
        syn();
    } else if (!strcmp(argv[1], "swipe") && argc == 6) {
        int x1 = fx(parse_coord(argv[2])), y1 = fy(parse_coord(argv[3]));
        int x2 = fx(parse_coord(argv[4])), y2 = fy(parse_coord(argv[5]));
        emit(EV_ABS, ABS_MT_SLOT, 0);
        emit(EV_ABS, ABS_MT_TRACKING_ID, 1);
        emit(EV_ABS, ABS_MT_POSITION_X, x1);
        emit(EV_ABS, ABS_MT_POSITION_Y, y1);
        emit(EV_KEY, BTN_TOUCH, 1);
        syn();
        int steps = 16;
        for (int i = 1; i <= steps; i++) {
            emit(EV_ABS, ABS_MT_POSITION_X, x1 + (x2 - x1) * i / steps);
            emit(EV_ABS, ABS_MT_POSITION_Y, y1 + (y2 - y1) * i / steps);
            syn();
            usleep(12000);
        }
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(EV_KEY, BTN_TOUCH, 0);
        syn();
    } else {
        usage(argv[0]);
        close(g_fd);
        return 2;
    }

    close(g_fd);
    return g_err ? 1 : 0;
}
