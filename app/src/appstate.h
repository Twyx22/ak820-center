/*
 * appstate.h — shared application state (keymap layers, custom RGB, macros).
 * The official app persists client-side (config.xml); the keyboard only
 * receives writes. Here: JSON in ~/.config/ak820-center/state.json.
 */
#pragma once

#include "device.h"
#include <QObject>
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

class AppState : public QObject {
    Q_OBJECT
public:
    static AppState *instance();

    /* Couche 0 = base, 1 = Fn */
    std::array<KeyAction, LED_COUNT> layer[2];
    QVector<Macro> macros;
    std::array<uint8_t, LED_COUNT * 3> customRgb{};

    bool loadState();
    bool saveState() const;

    /* Encodage de la charge macro (index 400 o + blocs) — FUN_0042d6f0. */
    QVector<uint8_t> encodeMacroPayload(bool *ok) const;

    static QString actionLabel(const KeyAction &a);

signals:
    void macrosChanged();
    void layersChanged();
    void rgbChanged();

private:
    AppState();
    QString statePath() const;
};

} /* namespace ak820 */
