/*
 * device.h — AK820 Pro HID communication layer ("legacy" 0xFF13 protocol)
 * See ../PROTOCOL.md. Decompiled from DeviceDriver.exe 1.0.0.5.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMutex>
#include <array>
#include <cstdint>

namespace ak820 {

static constexpr int PACKET_LEN = 64;
using Packet = std::array<uint8_t, PACKET_LEN>;

/* T2 transport: 33-byte packets (report ID 0 + 32-byte payload), checksum at byte 8. */
static constexpr int T2_PACKET_LEN = 33;
using T2Packet = std::array<uint8_t, T2_PACKET_LEN>;

/* Onboard lighting modes (20 + custom 0x80). */
enum LightingMode : uint8_t {
    ModeOff = 0x00, ModeStatic = 0x01, ModeSingleOn = 0x02, ModeSingleOff = 0x03,
    ModeGlittering = 0x04, ModeFalling = 0x05, ModeColourful = 0x06, ModeBreath = 0x07,
    ModeSpectrum = 0x08, ModeOutward = 0x09, ModeScrolling = 0x0A, ModeRolling = 0x0B,
    ModeRotating = 0x0C, ModeExplode = 0x0D, ModeLaunch = 0x0E, ModeRipples = 0x0F,
    ModeFlowing = 0x10, ModePulsating = 0x11, ModeTilt = 0x12, ModeShuttle = 0x13,
    ModeCustom = 0x80,
};

struct LightingConfig {
    uint8_t mode = ModeStatic;
    uint8_t r = 0, g = 0, b = 255;
    uint8_t colorful = 0;   /* 0 = fixed, 1 = rainbow, 2 = reversed */
    uint8_t brightness = 5; /* 0..5 */
    uint8_t speed = 3;      /* 0..5 */
    uint8_t direction = 0;  /* 0=L, 1=D, 2=U, 3=R */
};

struct BatteryStatus {
    bool valid = false;
    bool wiredFull = false;
    int percent = -1;
};

struct KeyAction {
    uint8_t page = 0x00;  /* 0=default 1=mouse 2=keyboard 3=multimedia 5=special 6=macro 7=extended */
    uint8_t p1 = 0, p2 = 0, p3 = 0;
    bool operator==(const KeyAction &o) const {
        return page == o.page && p1 == o.p1 && p2 == o.p2 && p3 == o.p3;
    }
};

/* Number of matrix slots (keymap/RGB banks, 144-LED layout). */
static constexpr int LED_COUNT = 144;

/* Which transport the keyboard is currently reached through. */
enum class ConnectionKind {
    None,
    Wired,   /* USB cable: 65-byte feature reports (T1, usage page 0xFF13) */
    Dongle,  /* 2.4G dongle: 33-byte output/input reports (T2, usage page 0xFF60) */
};

class Device : public QObject {
    Q_OBJECT
public:
    static Device *instance();

    bool isConnected() const { return m_dev != nullptr || m_dongle != nullptr; }
    ConnectionKind connectionKind() const;
    QString lastError() const { return m_error; }
    QString devicePath() const { return m_path; }

    /* Opens the 0xFF13 control interface (wired 8009/800A). USB wired only. */
    bool connectKeyboard();
    /* EXPERIMENTAL, pending hardware validation: opens the 2.4G dongle
       control interface (PID FDFD, usage page 0xFF60; MI_03 preferred,
       any 0xFF60 interface accepted). */
    bool connectDongle();
    /* Closes whatever transport is open (wired handle, dongle handle, or both). */
    void disconnectKeyboard();

    /* Generic session: START(flag=1) → cmd → data → COMMIT → FINISH. */
    bool writeSetting(uint8_t cmd, uint8_t sub, uint16_t value,
                      const Packet *data = nullptr);
    /* High-level operations. */
    bool setLighting(const LightingConfig &cfg);
    bool setSleep(int code);                       /* 0,1,2,3 = never,1,5,30 min */
    bool syncClock();                              /* system clock → keyboard */
    bool writeKeymapLayer(int layer, const std::array<KeyAction, LED_COUNT> &keySlots); /* 0=base 1=Fn */
    /* EXPERIMENTAL, pending hardware validation: single-key write over the
       dongle T2 transport — [00 03|0D 04 <slot> <page p1 p2 p3> <csum>]
       (03 = base layer, 0D = Fn layer). Requires the dongle transport. */
    bool writeKeySlot(int layer, int slot, const KeyAction &a);
    bool writeCustomRgb(const std::array<uint8_t, LED_COUNT * 3> &rgb);
    bool readCustomRgb(std::array<uint8_t, LED_COUNT * 3> &rgb);
    bool openMacroSession();
    bool writeMacroPayload(const QVector<uint8_t> &payload, int blockCount);
    /* EXPERIMENTAL, pending hardware validation: official "Real-time Lighting"
       feature (language key 55) — T2 cmd 0x14, 21 parameters of 28 bytes
       (last 16 bytes) covering 144 LEDs. Requires the dongle transport.
       rgb must point to LED_COUNT * 3 bytes. */
    bool pushRealtimeFrame(const uint8_t *rgb);
    /* Routes through the dongle transport when connected via dongle. */
    BatteryStatus battery();

    /* TFT screen: open the session (START flag 0 + cmd 0x72), then the 4096-B
       chunks go out on the TFT interface, then tftCommit(). */
    bool tftBegin(int slot, int chunkCount);
    bool tftCommit();
    /* Sends FINISH to release the keyboard after a failed session. */
    void abortSession(uint16_t flag = 0);

signals:
    void connectedChanged(bool connected);

private:
    Device();
    ~Device() override;

    bool connectLocked();                 /* no lock — mutex already held */
    bool connectDongleLocked();           /* no lock — mutex already held */
    bool exchange(Packet &pkt);           /* T1: 65-byte feature report */
    bool t2Exchange(T2Packet &pkt);       /* T2: 33-byte output report + checksum, dongle */
    BatteryStatus batteryDongle();        /* T1 battery request over the dongle transport */
    bool startSession(uint16_t flag, int attempts = 10);
    bool finishSession(uint16_t flag);
    bool commit();
    bool sendMulti(const uint8_t *data, int len); /* 64-byte multi-packet send */

    void *m_dev = nullptr;      /* hid_device* (wired) */
    void *m_dongle = nullptr;   /* hid_device* (2.4G dongle) */
    QString m_error;
    QString m_path;
    QMutex m_mutex;
};

} /* namespace ak820 */
