/*
 * appstate.h — shared application state (keymap layers, custom RGB, macros,
 * lighting, named profiles). The official app persists client-side
 * (config.xml); the keyboard only receives writes. Here: JSON in
 * ~/.config/ak820-center/state.json.
 */
#pragma once

#include "device.h"
#include <QObject>
#include <QList>
#include <QVector>
#include <array>

namespace ak820 {

struct MacroAction {
    uint16_t delayMs = 0;
    uint8_t key = 0;
    uint8_t flags = 0; /* B0=key press, 30=release, 90/10=mouse, 50=delay */
};

struct Macro {
    QString name;
    QVector<MacroAction> actions;
};

/* Named snapshot of the whole live configuration ("My Exclusive Config" in
   the official app). Stored client-side; applying one overwrites the live
   members. */
struct Profile {
    QString name;
    LightingConfig lighting;
    std::array<KeyAction, LED_COUNT> layerBase;
    std::array<KeyAction, LED_COUNT> layerFn;
    QVector<Macro> macros;
    std::array<uint8_t, LED_COUNT * 3> customRgb{};
    int tftSlot = 1;
};

class AppState : public QObject {
    Q_OBJECT
public:
    static AppState *instance();

    /* Layer 0 = base, 1 = Fn */
    std::array<KeyAction, LED_COUNT> layer[2];
    QVector<Macro> macros;
    std::array<uint8_t, LED_COUNT * 3> customRgb{};
    LightingConfig lighting;

    QList<Profile> profiles;

    bool loadState();
    bool saveState() const;

    /* Macro payload encoding (400-byte index + blocks) — FUN_0042d6f0. */
    QVector<uint8_t> encodeMacroPayload(bool *ok) const;

    static QString actionLabel(const KeyAction &a);

    /* Replaces the lighting config, notifies and persists. */
    void setLightingConfig(const LightingConfig &c);

    /* Profiles. saveProfile/importProfile return the new profile index,
       -1 on failure; applyProfile/deleteProfile/exportProfile report
       success. Applying copies the profile into the live members, emits
       lightingChanged/layersChanged/macrosChanged/rgbChanged and persists. */
    int saveProfile(const QString &name);
    bool applyProfile(int index);
    bool deleteProfile(int index);
    bool exportProfile(int index, const QString &path) const;
    int importProfile(const QString &path);

signals:
    void macrosChanged();
    void layersChanged();
    void rgbChanged();
    void lightingChanged();
    void profilesChanged();

private:
    AppState();
    QString statePath() const;
};

} /* namespace ak820 */
