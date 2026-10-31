/*
 * appstate.h — état applicatif partagé (couches keymap, RGB custom, macros).
 * L'app officielle persiste côté client (config.xml) ; le clavier ne reçoit
 * que des écritures. Ici : JSON dans ~/.config/ak820-center/state.json.
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
    uint8_t flags = 0; /* B0=press clavier, 30=release, 90/10 souris, 50=délai */
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
