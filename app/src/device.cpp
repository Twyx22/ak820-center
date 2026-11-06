/*
 * device.cpp — AK820 Pro legacy protocol implementation (see ../PROTOCOL.md).
 * T1: 65-byte feature reports on the wired 0xFF13 interface.
 * T2: 33-byte output/input reports with checksum on the 2.4G dongle 0xFF60
 *     interface (EXPERIMENTAL, pending hardware validation).
 */
#include "device.h"

#include <hidapi/hidapi.h>
#include <QDateTime>
#include <QThread>

namespace ak820 {

static const uint16_t PIDS[] = { 0x8009, 0x800A };
static constexpr uint16_t VID = 0x0C45;
static constexpr uint16_t PID_DONGLE = 0xFDFD;
static constexpr int USAGE_PAGE_CONTROL = 0xFF13;
static constexpr int USAGE_PAGE_DONGLE = 0xFF60;
static constexpr int DONGLE_CONFIG_IFACE = 3;   /* MI_03 preferred */
static constexpr int CMD_DELAY_MS = 35;   /* cmd_delaytime from the official config.xml */
static constexpr int READ_DELAY_MS = 50;

static constexpr uint8_t CMD_COMMIT = 0x02;
static constexpr uint8_t CMD_KEYMAP_BASE = 0x11;
static constexpr uint8_t CMD_LED_EFFECT = 0x13;
static constexpr uint8_t CMD_SLEEP = 0x17;
static constexpr uint8_t CMD_START = 0x18;
static constexpr uint8_t CMD_MACRO_OPEN = 0x19;
static constexpr uint8_t CMD_MACRO_LEN = 0x15;
static constexpr uint8_t CMD_RGB_BANK = 0x23;
static constexpr uint8_t CMD_KEYMAP_FN = 0x27;
static constexpr uint8_t CMD_CLOCK = 0x28;
static constexpr uint8_t CMD_RGB_READ = 0xF5;
static constexpr uint8_t CMD_FINISH = 0xF0;

/* T2 (dongle) command bytes and framing — see PROTOCOL.md "T2 commands". */
static constexpr uint8_t T2_CMD_REALTIME_LIGHT = 0x14;
static constexpr uint8_t T2_KEY_LAYER_BASE = 0x03;
static constexpr uint8_t T2_KEY_LAYER_FN = 0x0D;
static constexpr int T2_CHECKSUM_OFFSET = 8;    /* FUN_0044f5f0 */
static constexpr int T2_RETRIES = 20;
static constexpr int T2_RETRY_DELAY_MS = 10;
static constexpr int T2_PARAM_COUNT = 21;       /* 144-LED layout: 21 params */
static constexpr int T2_PARAM_SIZE = 28;        /* 28 bytes each... */
static constexpr int T2_PARAM_LAST_SIZE = 16;   /* ...16 bytes for the last one */

Device *Device::instance() {
    static Device s_instance;
    return &s_instance;
}

Device::Device() = default;
Device::~Device() { disconnectKeyboard(); }

ConnectionKind Device::connectionKind() const {
    if (m_dongle) return ConnectionKind::Dongle;
    if (m_dev) return ConnectionKind::Wired;
    return ConnectionKind::None;
}

void Device::disconnectKeyboard() {
    QMutexLocker lock(&m_mutex);
    const bool wasConnected = m_dev || m_dongle;
    if (m_dev) {
        hid_close((hid_device *)m_dev);
        m_dev = nullptr;
    }
    if (m_dongle) {
        hid_close((hid_device *)m_dongle);
        m_dongle = nullptr;
    }
    if (wasConnected) {
        m_path.clear();
        emit connectedChanged(false);
    }
    hid_exit();
}

bool Device::connectLocked() {
    if (m_dev) return true;
    if (hid_init() < 0) {
        m_error = QStringLiteral("hid_init failed");
        return false;
    }
    for (uint16_t pid : PIDS) {
        struct hid_device_info *list = hid_enumerate(VID, pid), *it;
        for (it = list; it; it = it->next) {
            if (it->usage_page != USAGE_PAGE_CONTROL) continue;
            hid_device *dev = hid_open_path(it->path);
            if (!dev) continue;
            m_dev = dev;
            m_path = QString::fromUtf8(it->path);
            break;
        }
        hid_free_enumeration(list);
        if (m_dev) {
            emit connectedChanged(true);
            return true;
        }
    }
    m_error = QStringLiteral("Keyboard not found (vendor interface 0xFF13). "
                             "Plug the USB-C cable (wired mode).");
    hid_exit();
    return false;
}

bool Device::connectKeyboard() {
    QMutexLocker lock(&m_mutex);
    return connectLocked(); /* connectLocked() already emits connectedChanged(true) */
}

bool Device::connectDongle() {
    QMutexLocker lock(&m_mutex);
    return connectDongleLocked();
}

bool Device::connectDongleLocked() {
    /* EXPERIMENTAL, pending hardware validation — see PROTOCOL.md "2.4G dongle".
       The dongle exposes no feature reports: MI_03 (preferred) carries
       32-byte output/input reports used by the T2 command transport. */
    if (m_dongle) return true;
    if (hid_init() < 0) {
        m_error = QStringLiteral("hid_init failed");
        return false;
    }
    struct hid_device_info *list = hid_enumerate(VID, PID_DONGLE), *it;
    /* Pass 1: the MI_03 config interface; pass 2: any 0xFF60 interface. */
    for (int pass = 0; pass < 2 && !m_dongle; pass++) {
        for (it = list; it; it = it->next) {
            if (it->usage_page != USAGE_PAGE_DONGLE) continue;
            if (pass == 0 && it->interface_number != DONGLE_CONFIG_IFACE) continue;
            hid_device *dev = hid_open_path(it->path);
            if (!dev) continue;
            m_dongle = dev;
            m_path = QString::fromUtf8(it->path);
            break;
        }
    }
    hid_free_enumeration(list);
    if (m_dongle) {
        emit connectedChanged(true);
        return true;
    }
    m_error = QStringLiteral("2.4G dongle not found (interface 0xFF60).");
    hid_exit();
    return false;
}

bool Device::exchange(Packet &pkt) {
    if (!m_dev) {
        m_error = QStringLiteral("not connected");
        return false;
    }
    unsigned char wire[65] = { 0 };
    unsigned char resp[65] = { 0 };
    memcpy(wire + 1, pkt.data(), PACKET_LEN);
    QThread::msleep(CMD_DELAY_MS);
    if (hid_send_feature_report((hid_device *)m_dev, wire, 65) < 0) {
        m_error = QString::fromWCharArray(hid_error((hid_device *)m_dev));
        return false;
    }
    QThread::msleep(READ_DELAY_MS);
    int n = hid_get_feature_report((hid_device *)m_dev, resp, 65);
    if (n < 0) {
        m_error = QString::fromWCharArray(hid_error((hid_device *)m_dev));
        return false;
    }
    memcpy(pkt.data(), resp + 1, PACKET_LEN);
    return true;
}

bool Device::t2Exchange(T2Packet &pkt) {
    /* EXPERIMENTAL, pending hardware validation — T2 transport (FUN_0044f5f0).
       pkt is [report ID 0][32-byte payload]; the checksum is the byte sum of
       the prepared 65-byte buffer (checksum field zeroed during the sum),
       stored at byte 8. The packet goes out as a 33-byte output report;
       the reply is validated by its bytes 1..3 echoing the request, with
       20 retries and 10 ms sleeps. */
    if (!m_dongle) {
        m_error = QStringLiteral("not connected (2.4G dongle required)");
        return false;
    }
    unsigned char prepared[65] = { 0 };
    memcpy(prepared, pkt.data(), T2_PACKET_LEN);
    prepared[T2_CHECKSUM_OFFSET] = 0;
    uint8_t sum = 0;
    for (int i = 0; i < 65; i++) sum = (uint8_t)(sum + prepared[i]);
    pkt[T2_CHECKSUM_OFFSET] = sum;

    /* One write retry after 10 ms, like FUN_0044f5f0. */
    if (hid_write((hid_device *)m_dongle, pkt.data(), T2_PACKET_LEN) < 0) {
        QThread::msleep(10);
        if (hid_write((hid_device *)m_dongle, pkt.data(), T2_PACKET_LEN) < 0) {
            m_error = QString::fromWCharArray(hid_error((hid_device *)m_dongle));
            return false;
        }
    }
    QThread::msleep(5);
    for (int attempt = 0; attempt < T2_RETRIES; attempt++) {
        unsigned char resp[T2_PACKET_LEN] = { 0 };
        int n = hid_read_timeout((hid_device *)m_dongle, resp, T2_PACKET_LEN, 50);
        if (n >= 4 && resp[1] == pkt[1] && resp[2] == pkt[2] && resp[3] == pkt[3]) {
            memcpy(pkt.data(), resp, T2_PACKET_LEN);
            return true;
        }
        QThread::msleep(T2_RETRY_DELAY_MS);
    }
    m_error = QStringLiteral("No valid dongle reply after %1 retries").arg(T2_RETRIES);
    return false;
}

static void ctrlPkt(Packet &p, uint8_t cmd, uint8_t sub, uint16_t value) {
    p.fill(0);
    p[0] = 0x04;
    p[1] = cmd;
    p[2] = sub;
    p[8] = value & 0xff;
    p[9] = value >> 8;
}

static bool ackOk(const Packet &p, uint8_t cmd) {
    return p[0] == 4 && p[1] == cmd && p[3] == 1;
}

bool Device::startSession(uint16_t flag, int attempts) {
    Packet p;
    for (int i = 0; i < attempts; i++) {
        ctrlPkt(p, CMD_START, 0, flag);
        if (!exchange(p)) return false;
        bool empty = true;
        for (uint8_t b : p) if (b) { empty = false; break; }
        if (ackOk(p, CMD_START)) return true;
        if (!empty && p[3] != 0xFF) return true; /* non-busy reply */
        QThread::msleep(300);
    }
    m_error = QStringLiteral("Keyboard refuses to open a session (busy?)");
    return false;
}

bool Device::commit() {
    Packet p;
    ctrlPkt(p, CMD_COMMIT, 0, 0);
    if (!exchange(p)) return false;
    if (!ackOk(p, CMD_COMMIT)) {
        m_error = QStringLiteral("COMMIT rejected (status %1)").arg(p[3], 2, 16, QChar('0'));
        return false;
    }
    return true;
}

bool Device::finishSession(uint16_t flag) {
    Packet p;
    ctrlPkt(p, CMD_FINISH, 0, flag);
    if (!exchange(p)) return false;
    if (!ackOk(p, CMD_FINISH)) {
        m_error = QStringLiteral("FINISH rejected (status %1)").arg(p[3], 2, 16, QChar('0'));
        return false;
    }
    return true;
}

bool Device::sendMulti(const uint8_t *data, int len) {
    /* The official driver sends len>>6 packets of 64 B with 10 retries (FUN_0044eed0) */
    int packets = len / PACKET_LEN;
    for (int i = 0; i < packets; i++) {
        Packet p;
        memcpy(p.data(), data + i * PACKET_LEN, PACKET_LEN);
        bool sent = false;
        for (int retry = 0; retry < 10 && !sent; retry++)
            sent = exchange(p);
        if (!sent) return false;
        QThread::msleep(2);
    }
    return true;
}

bool Device::writeSetting(uint8_t cmd, uint8_t sub, uint16_t value, const Packet *data) {
    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    if (!startSession(1)) { finishSession(1); return false; }
    Packet p;
    ctrlPkt(p, cmd, sub, value);
    if (!exchange(p)) { finishSession(1); return false; }
    if (!ackOk(p, cmd)) {
        m_error = QStringLiteral("Command %1 rejected (status %2)")
                      .arg(cmd, 2, 16, QChar('0')).arg(p[3], 2, 16, QChar('0'));
        finishSession(1);
        return false;
    }
    if (data) {
        Packet d = *data;
        if (!exchange(d)) { finishSession(1); return false; }
    }
    if (!commit()) { finishSession(1); return false; }
    if (!finishSession(1)) return false;
    return true;
}

bool Device::setLighting(const LightingConfig &cfg) {
    /* Modes = exact list of the official driver UI (combo lan 370-378):
       0x00 Off, 0x01 Static On, 0x04 Starlight, 0x05 Fluttering,
       0x07 Dynamic Breathing, 0x09 Colorful Fountain, 0x0A Rainbow Wave,
       0x0B Following Currents, 0x0C Peak Revolving, 0x80 custom.
       Modes are sent as-is (no remapping). */
    Packet d;
    d.fill(0);
    d[0] = cfg.mode;
    d[1] = cfg.r; d[2] = cfg.g; d[3] = cfg.b;
    /* colorful: 0 = fixed, 1 = rainbow, 2 = reversed rainbow
       ("colorful" column of t_light_data in the official driver). */
    d[8] = cfg.colorful;
    d[9] = cfg.brightness;
    d[10] = cfg.speed;
    d[11] = cfg.direction;
    /* Official driver trailer: AA 55 (0x55aa LE) — 0x42b100/0x434090. */
    d[14] = 0xAA; d[15] = 0x55;
    return writeSetting(CMD_LED_EFFECT, 0, 1, &d);
}

bool Device::setSleep(int code) {
    Packet d;
    d.fill(0);
    d[8] = (uint8_t)code;
    d[62] = 0xAA; d[63] = 0x55;
    return writeSetting(CMD_SLEEP, 1, 1, &d);
}

bool Device::syncClock() {
    QDateTime now = QDateTime::currentDateTime();
    Packet d;
    d.fill(0);
    d[0] = 0x00; d[1] = 0x01; d[2] = 0x5A;
    d[3] = (uint8_t)(now.date().year() % 100);
    d[4] = (uint8_t)now.date().month();
    d[5] = (uint8_t)now.date().day();
    d[6] = (uint8_t)now.time().hour();
    d[7] = (uint8_t)now.time().minute();
    d[8] = (uint8_t)now.time().second();
    d[9] = 0;
    d[10] = 0x04;
    d[62] = 0xAA; d[63] = 0x55;
    return writeSetting(CMD_CLOCK, 0, 1, &d);
}

bool Device::writeKeymapLayer(int layer, const std::array<KeyAction, LED_COUNT> &keySlots) {
    /* 576 bytes: 144 slots of 4 bytes [page, p1, p2, p3] (FUN_00418420), AA 55 trailer. */
    static uint8_t buf[PACKET_LEN * 9];
    memset(buf, 0, sizeof buf);
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t *s = buf + i * 4;
        s[0] = keySlots[i].page;
        s[1] = keySlots[i].p1;
        s[2] = keySlots[i].p2;
        s[3] = keySlots[i].p3;
    }
    buf[574] = 0xAA; buf[575] = 0x55;

    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    if (!startSession(1)) { finishSession(1); return false; }
    Packet p;
    ctrlPkt(p, layer == 0 ? CMD_KEYMAP_BASE : CMD_KEYMAP_FN, 0, 9);
    if (!exchange(p)) { finishSession(1); return false; }
    if (!ackOk(p, layer == 0 ? CMD_KEYMAP_BASE : CMD_KEYMAP_FN)) {
        m_error = QStringLiteral("Keymap command rejected (status %1)").arg(p[3], 2, 16, QChar('0'));
        finishSession(1);
        return false;
    }
    if (!sendMulti(buf, 576)) { finishSession(1); return false; }
    if (!commit()) { finishSession(1); return false; }
    if (!finishSession(1)) return false;
    return true;
}

bool Device::writeKeySlot(int layer, int slot, const KeyAction &a) {
    /* EXPERIMENTAL, pending hardware validation — single-key write over the
       dongle T2 transport: [00 03|0D 04 <slot> <page p1 p2 p3> <csum>]
       (03 = base layer, 0D = Fn layer). */
    QMutexLocker lock(&m_mutex);
    if (!m_dongle && !connectDongleLocked()) return false;
    T2Packet pkt;
    pkt.fill(0);
    pkt[1] = layer == 0 ? T2_KEY_LAYER_BASE : T2_KEY_LAYER_FN;
    pkt[2] = 0x04;
    pkt[3] = (uint8_t)slot;
    pkt[4] = a.page;
    pkt[5] = a.p1;
    pkt[6] = a.p2;
    pkt[7] = a.p3;
    return t2Exchange(pkt);
}

bool Device::writeCustomRgb(const std::array<uint8_t, LED_COUNT * 3> &rgb) {
    static uint8_t buf[PACKET_LEN * 9];
    memset(buf, 0, sizeof buf);
    for (int i = 0; i < LED_COUNT; i++) {
        buf[i * 4 + 0] = (uint8_t)i;
        buf[i * 4 + 1] = rgb[i * 3 + 0];
        buf[i * 4 + 2] = rgb[i * 3 + 1];
        buf[i * 4 + 3] = rgb[i * 3 + 2];
    }
    buf[574] = 0xAA; buf[575] = 0x55;

    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    if (!startSession(0)) { finishSession(0); return false; }
    Packet p;
    ctrlPkt(p, CMD_RGB_BANK, 0, 9);
    if (!exchange(p)) { finishSession(0); return false; }
    if (!ackOk(p, CMD_RGB_BANK)) {
        m_error = QStringLiteral("RGB bank rejected (status %1)").arg(p[3], 2, 16, QChar('0'));
        finishSession(0);
        return false;
    }
    if (!sendMulti(buf, 576)) { finishSession(0); return false; }
    if (!commit()) { finishSession(0); return false; }
    if (!finishSession(0)) return false;
    return true;
}

bool Device::pushRealtimeFrame(const uint8_t *rgb) {
    /* EXPERIMENTAL, pending hardware validation — official "Real-time
       Lighting" feature (language key 55), RGB path of the decompiled apply
       (FUN_0044c000): T2 cmd 0x14 writes 21 parameters of 28 bytes (the
       21st carries only 16) — [report ID 0, 0x14, <size>, <idx>, <28 bytes
       of [id, r, g, b] LED data>] — covering 144 LEDs (144 x 4 = 576 B). */
    QMutexLocker lock(&m_mutex);
    if (!m_dongle && !connectDongleLocked()) return false;
    uint8_t frame[LED_COUNT * 4];
    for (int i = 0; i < LED_COUNT; i++) {
        frame[i * 4 + 0] = (uint8_t)i;
        frame[i * 4 + 1] = rgb[i * 3 + 0];
        frame[i * 4 + 2] = rgb[i * 3 + 1];
        frame[i * 4 + 3] = rgb[i * 3 + 2];
    }
    for (int idx = 0; idx < T2_PARAM_COUNT; idx++) {
        const int size = idx == T2_PARAM_COUNT - 1 ? T2_PARAM_LAST_SIZE : T2_PARAM_SIZE;
        T2Packet pkt;
        pkt.fill(0);
        pkt[1] = T2_CMD_REALTIME_LIGHT;
        pkt[2] = (uint8_t)size;
        pkt[3] = (uint8_t)idx;
        memcpy(pkt.data() + 4, frame + idx * T2_PARAM_SIZE, size);
        if (!t2Exchange(pkt)) return false;
        QThread::msleep(2); /* Sleep(2) between parameters */
    }
    return true;
}

bool Device::readCustomRgb(std::array<uint8_t, LED_COUNT * 3> &rgb) {
    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    Packet p;
    p.fill(0);
    p[0] = 4; p[1] = CMD_RGB_READ; p[8] = 9; /* 9 blocks for 144 LEDs */
    if (!exchange(p)) return false;
    /* The 1st block arrives in p; the rest is read as 65-byte feature reports. */
    memcpy(rgb.data(), p.data() + 1, PACKET_LEN - 1 > LED_COUNT * 3
               ? LED_COUNT * 3 : PACKET_LEN - 1);
    for (int b = 1; b < 9; b++) {
        Packet next;
        next.fill(0);
        if (!exchange(next)) return false;
        int off = PACKET_LEN * b - 1;
        int cpy = qMin(PACKET_LEN, (int)rgb.size() - off);
        if (cpy > 0) memcpy(rgb.data() + off, next.data(), cpy);
    }
    Packet c;
    ctrlPkt(c, CMD_COMMIT, 0, 0);
    exchange(c);
    return true;
}

bool Device::openMacroSession() {
    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    if (!startSession(1)) { finishSession(1); return false; }
    Packet p;
    ctrlPkt(p, CMD_MACRO_OPEN, 0, 0);
    if (!exchange(p)) { finishSession(1); return false; }
    if (!ackOk(p, CMD_MACRO_OPEN)) {
        m_error = QStringLiteral("Macro session rejected (status %1)").arg(p[3], 2, 16, QChar('0'));
        finishSession(1);
        return false;
    }
    return true;
}

bool Device::writeMacroPayload(const QVector<uint8_t> &payload, int blockCount) {
    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    Packet p;
    ctrlPkt(p, CMD_MACRO_LEN, 0, (uint16_t)(blockCount + 1));
    if (!exchange(p)) { finishSession(1); return false; }
    /* AA 55 trailer expected by the firmware after the payload (FUN_0042d6f0) */
    QVector<uint8_t> buf = payload;
    int padded = ((buf.size() + PACKET_LEN - 1) / PACKET_LEN) * PACKET_LEN;
    buf.resize(padded);
    int tail = padded + 2;
    buf.resize(tail);
    buf[padded] = 0xAA; buf[padded + 1] = 0x55;
    if (!sendMulti(buf.data(), tail)) { finishSession(1); return false; }
    QThread::msleep(30);
    Packet c;
    ctrlPkt(c, CMD_COMMIT, 0, 0);
    if (!exchange(c)) { finishSession(1); return false; }
    if (!ackOk(c, CMD_COMMIT)) {
        m_error = QStringLiteral("Macro COMMIT rejected (status %1)").arg(c[3], 2, 16, QChar('0'));
        finishSession(1);
        return false;
    }
    if (!finishSession(1)) return false;
    return true;
}

bool Device::tftBegin(int slot, int chunkCount) {
    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    if (!startSession(0)) { finishSession(0); return false; }
    Packet p;
    p.fill(0);
    p[0] = 4;
    p[1] = 0x72;
    p[2] = (uint8_t)slot;
    p[8] = chunkCount & 0xff;
    p[9] = (chunkCount >> 8) & 0xff;
    if (!exchange(p)) { finishSession(0); return false; }
    if (!ackOk(p, 0x72)) {
        m_error = QStringLiteral("04 72 rejected (status %1)").arg(p[3], 2, 16, QChar('0'));
        finishSession(0);
        return false;
    }
    return true;
}

bool Device::tftCommit() {
    QMutexLocker lock(&m_mutex);
    Packet c;
    ctrlPkt(c, CMD_COMMIT, 0, 0);
    if (!exchange(c)) { finishSession(0); return false; }
    if (!ackOk(c, CMD_COMMIT)) {
        m_error = QStringLiteral("TFT COMMIT rejected (status %1)").arg(c[3], 2, 16, QChar('0'));
        finishSession(0);
        return false;
    }
    return true;
}

void Device::abortSession(uint16_t flag) {
    QMutexLocker lock(&m_mutex);
    Packet p;
    ctrlPkt(p, CMD_FINISH, 0, flag);
    exchange(p);
}

BatteryStatus Device::batteryDongle() {
    /* The dongle has no feature reports: the T1 battery request [20 01] goes
       out as a raw 33-byte output report ([report ID 0][20 01][zeros]),
       answered by an input report [20 01 00 pct] (FF = wired full).
       No 04 prefix, exactly like the wired T1 request (PROTOCOL.md). */
    BatteryStatus st;
    unsigned char wire[T2_PACKET_LEN] = { 0 };
    unsigned char resp[T2_PACKET_LEN] = { 0 };
    wire[1] = 0x20; wire[2] = 0x01;
    QThread::msleep(CMD_DELAY_MS);
    if (hid_write((hid_device *)m_dongle, wire, T2_PACKET_LEN) < 0) {
        m_error = QString::fromWCharArray(hid_error((hid_device *)m_dongle));
        return st;
    }
    QThread::msleep(READ_DELAY_MS);
    int n = hid_read_timeout((hid_device *)m_dongle, resp, T2_PACKET_LEN, 1000);
    if (n >= 5 && resp[1] == 0x20 && resp[2] == 0x01) {
        st.valid = true;
        if (resp[4] == 0xFF) { st.wiredFull = true; st.percent = 100; }
        else if (resp[4] <= 100) st.percent = resp[4];
    }
    return st;
}

BatteryStatus Device::battery() {
    QMutexLocker lock(&m_mutex);
    if (m_dongle) return batteryDongle();
    BatteryStatus st;
    if (!connectLocked()) return st;
    unsigned char wire[65] = { 0 };
    unsigned char resp[65] = { 0 };
    wire[1] = 0x20; wire[2] = 0x01;
    QThread::msleep(CMD_DELAY_MS);
    if (hid_send_feature_report((hid_device *)m_dev, wire, 65) < 0) return st;
    QThread::msleep(READ_DELAY_MS);
    int n = hid_get_feature_report((hid_device *)m_dev, resp, 65);
    if (n >= 5 && resp[1] == 0x20 && resp[2] == 0x01) {
        st.valid = true;
        if (resp[4] == 0xFF) { st.wiredFull = true; st.percent = 100; }
        else if (resp[4] <= 100) st.percent = resp[4];
    }
    return st;
}

} /* namespace ak820 */
