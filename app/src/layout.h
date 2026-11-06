/*
 * layout.h — keyboard layout parsing (official rgb-keyboard.xml)
 */
#pragma once

#include <QString>
#include <QVector>
#include <QRect>

namespace ak820 {

struct LayoutKey {
    int code = 0;        /* USB HID usage (0x04..0xE7, 0xAF = Fn) */
    QString name;        /* label */
    int keyIndex = 0;    /* logical index (official id) */
    int lightIndex = 0;  /* LED slot in the keymap/RGB banks (0..143) */
    int row = 0, col = 0;
    QRect rect;          /* position in the 800x300 rendering */
    bool fnLayerDisable = false;
};

struct Layout {
    static Layout *instance();   /* singleton loaded from system paths */

    bool load(const QString &xmlPath);
    const QVector<LayoutKey> &keys() const { return m_keys; }
    const LayoutKey *keyByLightIndex(int li) const;
    int keyboardX = 0, keyboardY = 0, keyboardW = 800, keyboardH = 300;

private:
    QVector<LayoutKey> m_keys;
};

} /* namespace ak820 */
