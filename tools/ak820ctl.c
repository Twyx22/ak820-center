/*
 * ak820ctl — outil CLI de test/contrôle du protocole « legacy » AJAZZ AK820 Pro
 * issu de la décompilation de DeviceDriver.exe 1.0.0.5 (voir ../PROTOCOL.md).
 *
 * Build : gcc -O2 -Wall -o ak820ctl ak820ctl.c -lhidapi-hidraw
 */
#include <hidapi/hidapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VID 0x0C45
#define PKT 64

static const unsigned short PIDS[] = { 0x8009, 0x800A, 0xFDFD, 0xFEFE };

#define USAGE_PAGE_CONTROL 0xFF13
#define USAGE_PAGE_DONGLE  0xFF60
#define TFT_CHUNK 4096
static const unsigned char TFT_DATA_DESC[] = {
    0x06, 0x68, 0xff, 0x09, 0x61, 0xa1, 0x01, 0x09, 0x62, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x95, 0x40, 0x75, 0x08, 0x81, 0x02, 0x09, 0x63, 0x15, 0x00, 0x26, 0xff, 0x00, 0x96,
    0x00, 0x10, 0x75, 0x08, 0x91, 0x02, 0xc0
};

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void dump(const char *tag, const unsigned char *p, int n) {
    printf("%s (%d o):", tag, n);
    for (int i = 0; i < n; i++) {
        if (i % 16 == 0) printf("\n  %02x:", i);
        printf(" %02x", p[i]);
    }
    printf("\n");
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int parse_int(const char *s, int base, int lo, int hi, int *out) {
    char *end = NULL;
    long v;
    if (s == NULL || *s == '\0') return 0;
    v = strtol(s, &end, base);
    if (end == s || *end != '\0' || v < lo || v > hi) return 0;
    *out = (int)v;
    return 1;
}

/* Cherche l'interface de contrôle (0xFF13) pour le PID donné. */
static hid_device *open_control(unsigned short pid) {
    hid_device *dev = NULL;
    struct hid_device_info *list = hid_enumerate(VID, pid), *it;
    for (it = list; it; it = it->next) {
        printf("  PID %04X iface %d usage_page=0x%04X usage=0x%04X\n",
               pid, it->interface_number, it->usage_page, it->usage);
        if (it->usage_page == USAGE_PAGE_CONTROL && !dev)
            dev = hid_open_path(it->path);
    }
    hid_free_enumeration(list);
    return dev;
}

static hid_device *open_keyboard(void) {
    static const unsigned short pids[] = { 0x8009, 0x800A };
    hid_device *dev = NULL;
    for (unsigned i = 0; i < sizeof pids / sizeof *pids && !dev; i++)
        dev = open_control(pids[i]);
    if (!dev)
        fprintf(stderr, "clavier (interface 0xFF13) introuvable — brancher en filaire ?\n");
    return dev;
}

/* Interface de contrôle du dongle 2.4G (usage page 0xFF60, iface 3). */
static hid_device *open_dongle(void) {
    hid_device *dev = NULL;
    struct hid_device_info *list = hid_enumerate(VID, 0xFDFD), *it;
    for (it = list; it; it = it->next) {
        printf("  dongle iface %d usage_page=0x%04X usage=0x%04X\n",
               it->interface_number, it->usage_page, it->usage);
        if (it->usage_page == USAGE_PAGE_DONGLE && !dev) {
            dev = hid_open_path(it->path);
            if (!dev)
                fprintf(stderr, "  ouverture iface %d échouée: %ls\n",
                        it->interface_number, hid_error(NULL));
        }
    }
    hid_free_enumeration(list);
    if (!dev)
        fprintf(stderr, "dongle 2.4G (interface 0xFF60) introuvable\n");
    return dev;
}

/* T1 : échange feature 65 octets (04 xx) — cf PROTOCOL.md */
static int exchange(hid_device *dev, unsigned char pkt[PKT]) {
    unsigned char wire[65] = { 0 };
    unsigned char resp[65] = { 0 };
    int n;
    memcpy(wire + 1, pkt, PKT);
    msleep(35);
    if (hid_send_feature_report(dev, wire, 65) < 0) {
        fprintf(stderr, "send_feature_report: %ls\n", hid_error(dev));
        return -1;
    }
    msleep(50);
    n = hid_get_feature_report(dev, resp, 65);
    if (n < 0) {
        fprintf(stderr, "get_feature_report: %ls\n", hid_error(dev));
        return -1;
    }
    memcpy(pkt, resp + 1, PKT);
    return n;
}

static void ctrl_pkt(unsigned char pkt[PKT], unsigned char cmd, unsigned char sub, unsigned short value) {
    memset(pkt, 0, PKT);
    pkt[0] = 0x04;
    pkt[1] = cmd;
    pkt[2] = sub;
    pkt[8] = value & 0xff;
    pkt[9] = value >> 8;
}

static int ack_ok(const unsigned char p[PKT], unsigned char cmd) {
    return p[0] == 4 && p[1] == cmd && p[3] == 1;
}

/* START avec réessais (clavier occupé ~2 s après une session avortée) */
static int start_session(hid_device *dev, unsigned short flag) {
    unsigned char p[PKT];
    for (int i = 0; i < 10; i++) {
        ctrl_pkt(p, 0x18, 0, flag);
        exchange(dev, p);
        int empty = 1;
        for (int j = 0; j < PKT; j++) if (p[j]) { empty = 0; break; }
        if (ack_ok(p, 0x18))
            return 0;
        if (!empty && p[3] != 0xff)
            return 0; /* réponse non-busy */
        msleep(300);
    }
    fprintf(stderr, "START refusé\n");
    return -1;
}

static int finish(hid_device *dev, unsigned short flag) {
    unsigned char p[PKT];
    ctrl_pkt(p, 0xF0, 0, flag);
    exchange(dev, p);
    if (!ack_ok(p, 0xF0)) {
        fprintf(stderr, "FINISH: status %02x (attendu 01)\n", p[3]);
        return -1;
    }
    return 0;
}

static int commit(hid_device *dev) {
    unsigned char p[PKT];
    ctrl_pkt(p, 0x02, 0, 0);
    exchange(dev, p);
    if (!ack_ok(p, 0x02)) {
        fprintf(stderr, "COMMIT refusé (status %02x)\n", p[3]);
        return -1;
    }
    return 0;
}

static int cmd_probe(void) {
    if (hid_init()) { fprintf(stderr, "hid_init failed\n"); return 1; }
    for (unsigned i = 0; i < sizeof PIDS / sizeof *PIDS; i++) {
        printf("PID %04X:\n", PIDS[i]);
        struct hid_device_info *list = hid_enumerate(VID, PIDS[i]), *it;
        for (it = list; it; it = it->next)
            printf("  iface %d usage_page=0x%04X usage=0x%04X product=%ls\n",
                   it->interface_number, it->usage_page, it->usage,
                   it->product_string ? it->product_string : L"?");
        hid_free_enumeration(list);
    }
    hid_exit();
    return 0;
}

static int cmd_battery(int via_dongle) {
    hid_init();
    hid_device *dev = via_dongle ? open_dongle() : open_keyboard();
    if (!dev) { hid_exit(); return 1; }
    unsigned char wire[65] = { 0 };
    unsigned char resp[65] = { 0 };
    wire[1] = 0x20; wire[2] = 0x01;
    hid_send_feature_report(dev, wire, 65);
    msleep(50);
    int n = hid_get_feature_report(dev, resp, 65);
    if (n >= 5 && resp[1] == 0x20 && resp[2] == 0x01) {
        if (resp[4] == 0xFF) printf("batterie: filaire (pleine)\n");
        else if (resp[4] <= 100) printf("batterie: %d%%\n", resp[4]);
        else printf("batterie: valeur inattendue %02x\n", resp[4]);
    } else {
        printf("pas de lecture batterie (n=%d)\n", n);
        dump("rep", resp, n > 0 ? n : 0);
    }
    hid_close(dev);
    hid_exit();
    return 0;
}

/* Session générique : START → cmd → data → COMMIT → FINISH */
static int run_session(unsigned char cmd, unsigned char sub, unsigned short value,
                       void (*build_data)(unsigned char [PKT], const void *), const void *ctx) {
    hid_init();
    hid_device *dev = open_keyboard();
    unsigned char p[PKT];
    int rc = 1;
    if (!dev) { hid_exit(); return 1; }
    if (start_session(dev, 1) == 0) {
        ctrl_pkt(p, cmd, sub, value);
        exchange(dev, p);
        if (ack_ok(p, cmd)) {
            if (build_data) {
                build_data(p, ctx);
                exchange(dev, p);
            }
            rc = (commit(dev) == 0 && finish(dev, 1) == 0) ? 0 : 1;
        } else {
            fprintf(stderr, "cmd %02x refusée (status %02x)\n", cmd, p[3]);
            finish(dev, 1);
        }
    }
    hid_close(dev);
    hid_exit();
    return rc;
}

static void build_clock(unsigned char p[PKT], const void *ctx) {
    const struct tm *tmv = ctx;
    memset(p, 0, PKT);
    p[0] = 0x00; p[1] = 0x01; p[2] = 0x5A;
    p[3] = (unsigned char)(tmv->tm_year % 100);
    p[4] = (unsigned char)(tmv->tm_mon + 1);
    p[5] = (unsigned char)tmv->tm_mday;
    p[6] = (unsigned char)tmv->tm_hour;
    p[7] = (unsigned char)tmv->tm_min;
    p[8] = (unsigned char)tmv->tm_sec;
    p[9] = 0; p[10] = 0x04;
    p[62] = 0xAA; p[63] = 0x55;
}

static void build_sleep(unsigned char p[PKT], const void *ctx) {
    memset(p, 0, PKT);
    p[8] = *(const unsigned char *)ctx;
    p[62] = 0xAA; p[63] = 0x55;
}

static void build_light(unsigned char p[PKT], const void *ctx) {
    const unsigned char *a = ctx; /* mode,r,g,b,bright,speed,dir,colorful */
    memset(p, 0, PKT);
    p[0] = a[0];
    p[1] = a[1]; p[2] = a[2]; p[3] = a[3];
    p[8] = a[7]; /* colorful : 0 statique, 1 rainbow, 2 reverse */
    p[9] = a[4]; p[10] = a[5]; p[11] = a[6];
    p[14] = 0xAA; p[15] = 0x55; /* trailer du driver officiel */
}

static int cmd_readrgb(int blocks) {
    hid_init();
    hid_device *dev = open_keyboard();
    if (!dev) { hid_exit(); return 1; }
    unsigned char p[PKT];
    memset(p, 0, PKT);
    p[0] = 4; p[1] = 0xF5; p[8] = (unsigned char)clampi(blocks, 1, 9);
    if (exchange(dev, p) >= 0)
        dump("04 F5 rep (bloc 1)", p, PKT);
    hid_close(dev);
    hid_exit();
    return 0;
}

/* Trame de test TFT : 2 frames (rouge, bleue) 128×128 RGB565 LE. */
static int cmd_tfttest(int slot) {
    hid_init();
    hid_device *dev = open_keyboard();
    if (!dev) { hid_exit(); return 1; }

    /* payload = header 256 o + 2 frames de 32768 o, pad FF au multiple de 4096 */
    int frames = 2, frame_bytes = 128 * 128 * 2;
    int payload_len = 256 + frames * frame_bytes;
    int chunks = (payload_len + TFT_CHUNK - 1) / TFT_CHUNK;
    int padded = chunks * TFT_CHUNK;
    unsigned char *payload = calloc((size_t)padded, 1);
    if (!payload) { hid_exit(); return 1; }
    memset(payload, 0xFF, (size_t)padded);
    payload[0] = (unsigned char)frames;
    payload[1] = 100 / 2;      /* délai frame 0 : 100 ms */
    payload[2] = 100 / 2;      /* délai frame 1 */
    payload[3] = 0x00;
    /* frame 0 : rouge 0xF800, frame 1 : bleu 0x001F (RGB565 LE) */
    for (int f = 0; f < frames; f++) {
        unsigned int px = (f == 0) ? 0xF800u : 0x001Fu;
        unsigned char lo = px & 0xff, hi = px >> 8;
        unsigned char *dst = payload + 256 + f * frame_bytes;
        for (int i = 0; i < frame_bytes; i += 2) { dst[i] = lo; dst[i + 1] = hi; }
    }

    /* interface TFT (FF68 d'abord, puis FF67) */
    hid_device *tft = NULL;
    const unsigned short pages[] = { 0xFF68, 0xFF67 };
    for (unsigned pg = 0; pg < sizeof pages / sizeof *pages && !tft; pg++) {
        struct hid_device_info *list = hid_enumerate(VID, 0x8009), *it;
        for (it = list; it; it = it->next) {
            if (it->usage_page == pages[pg]) { tft = hid_open_path(it->path); break; }
        }
        hid_free_enumeration(list);
    }
    if (!tft) { fprintf(stderr, "interface TFT introuvable\n"); free(payload); hid_close(dev); hid_exit(); return 1; }

    /* vérifier le descripteur (garde-fou du driver officiel) */
    unsigned char desc[512] = { 0 };
    int dn = hid_get_report_descriptor(tft, desc, sizeof desc);
    if (dn != (int)sizeof TFT_DATA_DESC ||
        memcmp(desc, TFT_DATA_DESC, sizeof TFT_DATA_DESC) != 0) {
        fprintf(stderr, "descripteur TFT inattendu (n=%d) — envoi annulé\n", dn);
        for (int i = 0; i < dn; i++) printf("%02x ", desc[i]);
        printf("\n");
        free(payload); hid_close(tft); hid_close(dev); hid_exit(); return 1;
    }

    /* 1. session contrôle : START(0) → 04 72 <slot, blocs> ; chunks ; COMMIT */
    unsigned char p[PKT];
    if (start_session(dev, 0) != 0) goto fail;
    memset(p, 0, PKT);
    p[0] = 4; p[1] = 0x72; p[2] = (unsigned char)slot;
    p[8] = chunks & 0xff; p[9] = (chunks >> 8) & 0xff;
    exchange(dev, p);
    if (!ack_ok(p, 0x72)) {
        fprintf(stderr, "04 72 refusé (status %02x)\n", p[3]);
        goto fail;
    }
    printf("04 72 ack — envoi de %d blocs de 4096 o…\n", chunks);
    for (int i = 0; i < chunks; i++) {
        unsigned char rep[TFT_CHUNK + 1] = { 0 };
        memcpy(rep + 1, payload + i * TFT_CHUNK, TFT_CHUNK);
        if (hid_write(tft, rep, sizeof rep) != sizeof rep) {
            fprintf(stderr, "écriture chunk %d/%d échouée: %ls\n", i + 1, chunks, hid_error(tft));
            goto fail;
        }
        unsigned char ack[64];
        int n = hid_read_timeout(tft, ack, sizeof ack, 1000);
        if (n <= 0) {
            fprintf(stderr, "pas d'ack pour le chunk %d/%d\n", i + 1, chunks);
            goto fail;
        }
        printf("  chunk %d/%d ack (%d o)\n", i + 1, chunks, n);
    }
    ctrl_pkt(p, 0x02, 0, 0);
    exchange(dev, p);
    if (!ack_ok(p, 0x02)) {
        fprintf(stderr, "COMMIT refusé (status %02x)\n", p[3]);
        goto fail;
    }
    printf("✔ animation test envoyée (slot %d) — Fn+Suppr pour basculer l'affichage\n", slot);
    free(payload);
    hid_close(tft);
    hid_close(dev);
    hid_exit();
    return 0;
fail:
    ctrl_pkt(p, 0xF0, 0, 0);
    exchange(dev, p);
    free(payload);
    hid_close(tft);
    hid_close(dev);
    hid_exit();
    return 1;
}

static void usage(void) {
    fputs(
        "usage: ak820ctl probe\n"
        "       ak820ctl battery\n"
        "       ak820ctl clock\n"
        "       ak820ctl sleep <0|1|2|3>            (0,1,5,30 min)\n"
        "       ak820ctl light <mode> <rrggbb> [bright 0-5] [speed 0-5] [dir 0-3] [colorful 0-2]\n"
        "       ak820ctl readrgb <1..9>\n"
        "       ak820ctl tfttest [slot 1..3]   (trame rouge/bleue sur l'écran)\n",
        stderr);
}

/* Analyse "rrggbb" hexadécimal strict (6 chiffres) → r,g,b. */
static int parse_rgb(const char *s, int *r, int *g, int *b) {
    unsigned int v = 0;
    if (strlen(s) != 6) return 0;
    for (int i = 0; i < 6; i++) {
        char c = s[i];
        unsigned int d;
        if (c >= '0' && c <= '9')      d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A') + 10u;
        else return 0;
        v = (v << 4) | d;
    }
    *r = (int)((v >> 16) & 0xff);
    *g = (int)((v >> 8) & 0xff);
    *b = (int)(v & 0xff);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (!strcmp(argv[1], "probe"))   return cmd_probe();
    if (!strcmp(argv[1], "battery")) return cmd_battery(argc > 2 && !strcmp(argv[2], "dongle"));

    if (!strcmp(argv[1], "clock")) {
        time_t t = time(NULL);
        struct tm tmv;
        localtime_r(&t, &tmv);
        int rc = run_session(0x28, 0, 1, build_clock, &tmv);
        if (rc == 0)
            printf("horloge synchronisée: %04d-%02d-%02d %02d:%02d:%02d\n",
                   tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                   tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        return rc;
    }
    if (!strcmp(argv[1], "sleep") && argc > 2) {
        int code;
        if (!parse_int(argv[2], 10, 0, 3, &code)) { fprintf(stderr, "code veille: 0|1|2|3\n"); return 2; }
        return run_session(0x17, 1, 1, build_sleep, &code);
    }
    if (!strcmp(argv[1], "light") && argc > 3) {
        unsigned char a[8];
        int mode, r = 0, g = 0, b = 0, bright = 5, speed = 3, dir = 0, rain = 0;
        int ok = parse_int(argv[2], 0, 0, 0x80, &mode) && parse_rgb(argv[3], &r, &g, &b);
        if (ok && argc > 4) ok = parse_int(argv[4], 10, 0, 5, &bright);
        if (ok && argc > 5) ok = parse_int(argv[5], 10, 0, 5, &speed);
        if (ok && argc > 6) ok = parse_int(argv[6], 10, 0, 3, &dir);
        if (ok && argc > 7) ok = parse_int(argv[7], 10, 0, 1, &rain);
        if (!ok) { fprintf(stderr, "arguments light invalides\n"); return 2; }
        a[0] = (unsigned char)mode;
        a[1] = (unsigned char)r;
        a[2] = (unsigned char)g;
        a[3] = (unsigned char)b;
        a[4] = (unsigned char)bright;
        a[5] = (unsigned char)speed;
        a[6] = (unsigned char)dir;
        a[7] = (unsigned char)rain;
        int rc = run_session(0x13, 0, 1, build_light, a);
        if (rc == 0)
            printf("éclairage appliqué (mode %02x rgb %d,%d,%d bright=%d speed=%d dir=%d)\n",
                   mode, r, g, b, bright, speed, dir);
        return rc;
    }
    if (!strcmp(argv[1], "readrgb") && argc > 2) {
        int blocks;
        if (!parse_int(argv[2], 10, 1, 9, &blocks)) { fprintf(stderr, "blocs: 1..9\n"); return 2; }
        return cmd_readrgb(blocks);
    }
    if (!strcmp(argv[1], "tfttest")) {
        int slot = 1;
        if (argc > 2 && !parse_int(argv[2], 10, 1, 3, &slot)) { fprintf(stderr, "slot: 1..3\n"); return 2; }
        return cmd_tfttest(slot);
    }
    fprintf(stderr, "commande inconnue\n");
    return 2;
}
