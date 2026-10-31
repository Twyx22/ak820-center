/*
 * device.cpp — implémentation du protocole legacy AK820 Pro (cf PROTOCOL.md)
 */
#include "device.h"

#include <hidapi/hidapi.h>
#include <QDateTime>
#include <QThread>

namespace ak820 {

static const uint16_t PIDS[] = { 0x8009, 0x800A };
static constexpr uint16_t VID = 0x0C45;
static constexpr int USAGE_PAGE_CONTROL = 0xFF13;
static constexpr int CMD_DELAY_MS = 35;   /* cmd_delaytime du config.xml officiel */
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

Device *Device::instance() {
    static Device s_instance;
    return &s_instance;
}

Device::Device() = default;
Device::~Device() { disconnectKeyboard(); }

void Device::disconnectKeyboard() {
    QMutexLocker lock(&m_mutex);
    if (m_dev) {
        hid_close((hid_device *)m_dev);
        m_dev = nullptr;
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
    bool ok = connectLocked();
    if (ok) emit connectedChanged(true);
    return ok;
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
        if (!empty && p[3] != 0xFF) return true; /* réponse non-busy */
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
    /* Le driver officiel envoie len>>6 paquets de 64 o avec 10 réessais (FUN_0044eed0) */
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
    /* Modes = liste exacte de l'UI du driver officiel (combo lan 370-378) :
       0x00 Éteindre, 0x01 Static On, 0x04 Starlight, 0x05 Fluttering,
       0x07 Dynamic Breathing, 0x09 Colorful Fountain, 0x0A Rainbow Wave,
       0x0B Following Currents, 0x0C Peak Revolving, 0x80 custom.
       Les modes sont envoyés tels quels (pas de re-mapping). */
    Packet d;
    d.fill(0);
    d[0] = cfg.mode;
    d[1] = cfg.r; d[2] = cfg.g; d[3] = cfg.b;
    /* colorful : 0 = statique, 1 = arc-en-ciel, 2 = arc-en-ciel inversé
       (colonne "colorful" de t_light_data dans le driver officiel). */
    d[8] = cfg.colorful;
    d[9] = cfg.brightness;
    d[10] = cfg.speed;
    d[11] = cfg.direction;
    /* Trailer du driver officiel : AA 55 (0x55aa LE) — 0x42b100/0x434090. */
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
    /* 576 o : 144 slots de 4 o [page, p1, p2, p3] (FUN_00418420), trailer AA 55. */
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

bool Device::readCustomRgb(std::array<uint8_t, LED_COUNT * 3> &rgb) {
    QMutexLocker lock(&m_mutex);
    if (!connectLocked()) return false;
    Packet p;
    p.fill(0);
    p[0] = 4; p[1] = CMD_RGB_READ; p[8] = 9; /* 9 blocs pour 144 LED */
    if (!exchange(p)) return false;
    /* Le 1er bloc arrive dans p ; la suite se lit en feature reports 65 o. */
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
    /* Trailer AA 55 attendu par le firmware après le payload (FUN_0042d6f0) */
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

BatteryStatus Device::battery() {
    QMutexLocker lock(&m_mutex);
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
