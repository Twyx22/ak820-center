/*
 * device.h — couche communication HID AK820 Pro (protocole « legacy » 0xFF13)
 * Voir ../PROTOCOL.md. Décompilé de DeviceDriver.exe 1.0.0.5.
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

/* Modes d'éclairage embarqués (20 + custom 0x80). */
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
    uint8_t colorful = 0;   /* 0 = statique, 1 = arc-en-ciel, 2 = inversé */
    uint8_t brightness = 5; /* 0..5 */
    uint8_t speed = 3;      /* 0..5 */
    uint8_t direction = 0;  /* 0=G 1=B 2=H 3=D */
};

struct BatteryStatus {
    bool valid = false;
    bool wiredFull = false;
    int percent = -1;
};

struct KeyAction {
    uint8_t page = 0x00;  /* 0=défaut 1=souris 2=clavier 3=multimédia 5=special 6=macro 7=étendu */
    uint8_t p1 = 0, p2 = 0, p3 = 0;
    bool operator==(const KeyAction &o) const {
        return page == o.page && p1 == o.p1 && p2 == o.p2 && p3 == o.p3;
    }
};

/* Nombre de slots de la matrice (banques keymap/RGB, layout 144 LED). */
static constexpr int LED_COUNT = 144;

class Device : public QObject {
    Q_OBJECT
public:
    static Device *instance();

    bool isConnected() const { return m_dev != nullptr; }
    QString lastError() const { return m_error; }
    QString devicePath() const { return m_path; }

    /* Ouvre l'interface de contrôle 0xFF13 (filaire 8009/800A). */
    bool connectKeyboard();
    void disconnectKeyboard();

    /* Session générique : START(flag=1) → cmd → data → COMMIT → FINISH. */
    bool writeSetting(uint8_t cmd, uint8_t sub, uint16_t value,
                      const Packet *data = nullptr);
    /* Hautes opérations. */
    bool setLighting(const LightingConfig &cfg);
    bool setSleep(int code);                       /* 0,1,2,3 = jamais,1,5,30 min */
    bool syncClock();                              /* horloge système → clavier */
    bool writeKeymapLayer(int layer, const std::array<KeyAction, LED_COUNT> &keySlots); /* 0=base 1=Fn */
    bool writeCustomRgb(const std::array<uint8_t, LED_COUNT * 3> &rgb);
    bool readCustomRgb(std::array<uint8_t, LED_COUNT * 3> &rgb);
    bool openMacroSession();
    bool writeMacroPayload(const QVector<uint8_t> &payload, int blockCount);
    BatteryStatus battery();

    /* Écran TFT : ouvrir la session (START flag 0 + cmd 0x72), puis les chunks
       4096 o partent sur l'interface 0xFF67, puis tftCommit(). */
    bool tftBegin(int slot, int chunkCount);
    bool tftCommit();
    /* Envoie FINISH pour libérer le clavier après un échec de session. */
    void abortSession(uint16_t flag = 0);

signals:
    void connectedChanged(bool connected);

private:
    Device();
    ~Device() override;

    bool connectLocked();                 /* sans verrou — mutex déjà tenu */
    bool exchange(Packet &pkt);           /* T1 : 65 octets feature */
    bool startSession(uint16_t flag, int attempts = 10);
    bool finishSession(uint16_t flag);
    bool commit();
    bool sendMulti(const uint8_t *data, int len); /* multi-paquets 64 o */

    void *m_dev = nullptr;   /* hid_device* */
    QString m_error;
    QString m_path;
    QMutex m_mutex;
};

} /* namespace ak820 */
