/*
 * layout.h — parsing du layout clavier (XML officiel rgb-keyboard.xml)
 */
#pragma once

#include <QString>
#include <QVector>
#include <QRect>

namespace ak820 {

struct LayoutKey {
    int code = 0;        /* usage HID USB (0x04..0xE7, 0xAF = Fn) */
    QString name;        /* libellé */
    int keyIndex = 0;    /* index logique (id officiel) */
    int lightIndex = 0;  /* slot LED dans les banques keymap/RGB (0..143) */
    int row = 0, col = 0;
    QRect rect;          /* position dans le rendu 800x300 */
    bool fnLayerDisable = false;
};

struct Layout {
    static Layout *instance();   /* singleton chargé depuis les chemins système */

    bool load(const QString &xmlPath);
    const QVector<LayoutKey> &keys() const { return m_keys; }
    const LayoutKey *keyByLightIndex(int li) const;
    int keyboardX = 0, keyboardY = 0, keyboardW = 800, keyboardH = 300;

private:
    QVector<LayoutKey> m_keys;
};

} /* namespace ak820 */
