/*
 * dongletest.c — experiment the 2.4G dongle transport (0C45:FDFD, FF60).
 *
 * iface 3 (/dev/hidraw6): 32-byte in/out reports  -> small commands (0x21 transport)
 * iface 4 (/dev/hidraw7): 64-byte in/out reports  -> 04 xx command traffic (T1)
 *
 * The dongle interfaces have NO feature reports: writes are OUTPUT reports
 * (hid_write) and replies are INPUT reports (hid_read).
 *
 * Usage: dongletest [4|3] [battery|light|probe]
 */
#include <hidapi/hidapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VID 0x0C45
#define PID 0xFDFD

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void dump(const char *tag, const unsigned char *p, int n) {
    printf("%s:", tag);
    for (int i = 0; i < n; i++) {
        if (i % 16 == 0) printf("\n  %02x:", i);
        printf(" %02x", p[i]);
    }
    printf("\n");
}

static hid_device *open_dongle_iface(int iface) {
    hid_device *dev = NULL;
    struct hid_device_info *list = hid_enumerate(VID, PID), *it;
    for (it = list; it; it = it->next) {
        if (it->interface_number == iface && it->usage_page == 0xFF60) {
            dev = hid_open_path(it->path);
            if (dev) break;
        }
    }
    hid_free_enumeration(list);
    return dev;
}

/* write 64-byte output report, then read replies for `wait_ms` */
static int exchange64(hid_device *dev, const unsigned char pkt[64],
                      unsigned char replies[][64], int max_replies, int wait_ms) {
    unsigned char wire[65] = { 0 };
    memcpy(wire + 1, pkt, 64);
    if (hid_write(dev, wire, sizeof wire) != sizeof wire) {
        fprintf(stderr, "write failed: %ls\n", hid_error(dev));
        return -1;
    }
    int count = 0;
    int waited = 0;
    while (waited < wait_ms && count < max_replies) {
        int n = hid_read_timeout(dev, replies[count], 64, 200);
        if (n > 0) { count++; continue; }
        waited += 200;
    }
    return count;
}

/* 32-byte transport with checksum at byte 8 (FUN_0044f5f0 equivalent) */
static int exchange32(hid_device *dev, unsigned char pkt[33], int wait_ms) {
    unsigned char sum = 0;
    for (int i = 0; i < 33; i++) sum = (unsigned char)(sum + pkt[i]);
    pkt[8] = sum;
    unsigned char wire[34] = { 0 };
    memcpy(wire + 1, pkt, 33);
    if (hid_write(dev, wire, sizeof wire) != sizeof wire) {
        fprintf(stderr, "write32 failed: %ls\n", hid_error(dev));
        return -1;
    }
    unsigned char resp[33];
    int deadline = 0;
    while (deadline < wait_ms) {
        int n = hid_read_timeout(dev, resp, 33, 200);
        if (n > 0) {
            /* vendor validates bytes 1..3 echo */
            if (pkt[1] == resp[1] && pkt[2] == resp[2] && pkt[3] == resp[3]) {
                dump("32B reply", resp, 33);
                return 1;
            }
        }
        deadline += 200;
    }
    return 0;
}

int main(int argc, char **argv) {
    int iface = (argc > 1 && argv[1][0] == '3') ? 3 : 4;
    const char *cmd = argc > 2 ? argv[2] : "probe";
    hid_init();
    hid_device *dev = open_dongle_iface(iface);
    if (!dev) { fprintf(stderr, "dongle iface %d (0xFF60) not found\n", iface); return 1; }
    printf("dongle iface %d opened\n", iface);

    if (!strcmp(cmd, "probe")) {
        /* raw probe: send 04 18 START and dump whatever comes back */
        unsigned char pkt[64] = { 0 };
        pkt[0] = 0x04; pkt[1] = 0x18;
        unsigned char reps[4][64] = { 0 };
        int n = exchange64(dev, pkt, reps, 4, 1000);
        printf("START: %d reply(ies)\n", n);
        for (int i = 0; i < n; i++) dump("reply", reps[i], 64);
    } else if (!strcmp(cmd, "battery")) {
        unsigned char pkt[64] = { 0 };
        pkt[0] = 0x20; pkt[1] = 0x01;   /* no 04 prefix, like wired */
        unsigned char reps[4][64] = { 0 };
        int n = exchange64(dev, pkt, reps, 4, 1500);
        printf("battery: %d reply(ies)\n", n);
        for (int i = 0; i < n; i++) dump("reply", reps[i], 64);
    } else if (!strcmp(cmd, "light")) {
        /* full T1 session over output reports */
        unsigned char pkt[64] = { 0 };
        unsigned char reps[4][64] = { 0 };
        int n;
        pkt[0] = 0x04; pkt[1] = 0x18;
        n = exchange64(dev, pkt, reps, 4, 1500);
        printf("START: %d reply(ies)\n", n);
        for (int i = 0; i < n; i++) dump("reply", reps[i], 64);
        msleep(35);

        memset(pkt, 0, 64);
        pkt[0] = 0x04; pkt[1] = 0x13; pkt[8] = 1;
        n = exchange64(dev, pkt, reps, 4, 1500);
        printf("04 13: %d reply(ies)\n", n);
        for (int i = 0; i < n; i++) dump("reply", reps[i], 64);
        msleep(35);

        memset(pkt, 0, 64);
        pkt[0] = 0x01;            /* Static */
        pkt[1] = 0xFF; pkt[2] = 0x40; pkt[3] = 0x00;
        pkt[9] = 5; pkt[10] = 3; pkt[11] = 0;
        pkt[14] = 0xAA; pkt[15] = 0x55;
        n = exchange64(dev, pkt, reps, 4, 1500);
        printf("mode data: %d reply(ies)\n", n);
        for (int i = 0; i < n; i++) dump("reply", reps[i], 64);
        msleep(35);

        memset(pkt, 0, 64);
        pkt[0] = 0x04; pkt[1] = 0x02;
        n = exchange64(dev, pkt, reps, 4, 1500);
        printf("COMMIT: %d reply(ies)\n", n);
        for (int i = 0; i < n; i++) dump("reply", reps[i], 64);
        msleep(35);

        memset(pkt, 0, 64);
        pkt[0] = 0x04; pkt[1] = 0xF0;
        n = exchange64(dev, pkt, reps, 4, 1500);
        printf("FINISH: %d reply(ies)\n", n);
        for (int i = 0; i < n; i++) dump("reply", reps[i], 64);
    } else if (!strcmp(cmd, "cfg0f")) {
        /* iface 3: settings commit 0x0F via the 32-byte checksum transport */
        unsigned char pkt[33] = { 0 };
        pkt[1] = 0x0F;
        int ok = exchange32(dev, pkt, 1500);
        printf("0F: %s\n", ok ? "echo accepted" : "no valid reply");
    }
    hid_close(dev);
    hid_exit();
    return 0;
}
