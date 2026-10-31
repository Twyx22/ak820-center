#include "layout.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace ak820 {

Layout *Layout::instance() {
    static Layout s_layout;
    static bool s_loaded = false;
    if (!s_loaded) {
        s_loaded = true;
        for (const QString &dir : { QStringLiteral("/usr/share/ak820-center"),
                                    QStringLiteral("/usr/local/share/ak820-center"),
                                    QCoreApplication::applicationDirPath() + "/../share/ak820-center",
                                    QCoreApplication::applicationDirPath(),
                                    QStringLiteral(".") }) {
            QString p = dir + QStringLiteral("/rgb-keyboard.xml");
            if (QFileInfo::exists(p)) { s_layout.load(p); break; }
        }
    }
    return &s_layout;
}

bool Layout::load(const QString &xmlPath) {
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QXmlStreamReader xr(&f);
    m_keys.clear();
    while (!xr.atEnd()) {
        xr.readNext();
        if (xr.isStartElement() && xr.name() == u"Keyboard") {
            keyboardX = xr.attributes().value("xpos").toInt();
            keyboardY = xr.attributes().value("ypos").toInt();
            keyboardW = xr.attributes().value("width").toInt();
            keyboardH = xr.attributes().value("height").toInt();
        }
        if (xr.isStartElement() && xr.name() == u"key") {
            const auto &a = xr.attributes();
            LayoutKey k;
            k.code = (int)strtoul(a.value("code").toString().toLatin1().constData(), nullptr, 0);
            k.name = a.value("desc").toString();
            k.keyIndex = a.value("key_index").toInt();
            k.lightIndex = a.value("light_index").toInt();
            k.row = a.value("row_col").toString().section('#', 0, 0).toInt();
            k.col = a.value("row_col").toString().section('#', 1, 1).toInt();
            k.rect = QRect(a.value("rect_left").toInt(), a.value("rect_top").toInt(),
                           a.value("rect_width").toInt(), a.value("rect_height").toInt());
            k.fnLayerDisable = a.value("fnlayer_disable").toString() == "1";
            m_keys.append(k);
        }
    }
    return xr.hasError() ? false : !m_keys.isEmpty();
}

const LayoutKey *Layout::keyByLightIndex(int li) const {
    for (const LayoutKey &k : m_keys)
        if (k.lightIndex == li) return &k;
    return nullptr;
}

} /* namespace ak820 */
