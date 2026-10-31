#include "appstate.h"
#include "keycodes.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace ak820 {

AppState *AppState::instance() {
    static AppState s;
    return &s;
}

AppState::AppState() {
    customRgb.fill(0);
    loadState();
}

QString AppState::statePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/state.json");
}

bool AppState::loadState() {
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    QJsonObject root = doc.object();

    for (int l = 0; l < 2; l++) {
        QJsonArray la = root.value(l == 0 ? "layerBase" : "layerFn").toArray();
        for (int i = 0; i < la.size() && i < LED_COUNT; i++) {
            QJsonObject o = la[i].toObject();
            layer[l][i].page = (uint8_t)o.value("page").toInt();
            layer[l][i].p1 = (uint8_t)o.value("p1").toInt();
            layer[l][i].p2 = (uint8_t)o.value("p2").toInt();
            layer[l][i].p3 = (uint8_t)o.value("p3").toInt();
        }
    }
    QJsonArray ma = root.value("macros").toArray();
    macros.clear();
    for (const auto &v : ma) {
        QJsonObject o = v.toObject();
        Macro m;
        m.name = o.value("name").toString();
        for (const auto &av : o.value("actions").toArray()) {
            QJsonObject ao = av.toObject();
            MacroAction a;
            a.delayMs = (uint16_t)ao.value("delay").toInt();
            a.key = (uint8_t)ao.value("key").toInt();
            a.flags = (uint8_t)ao.value("flags").toInt();
            m.actions.append(a);
        }
        macros.append(m);
    }
    QJsonArray ra = root.value("customRgb").toArray();
    for (int i = 0; i < ra.size() && i < (int)customRgb.size(); i++)
        customRgb[i] = (uint8_t)ra[i].toInt();
    return true;
}

bool AppState::saveState() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QFile f(statePath());
    if (!f.open(QIODevice::WriteOnly)) return false;

    QJsonObject root;
    for (int l = 0; l < 2; l++) {
        QJsonArray la;
        for (int i = 0; i < LED_COUNT; i++) {
            QJsonObject o;
            o["page"] = layer[l][i].page;
            o["p1"] = layer[l][i].p1;
            o["p2"] = layer[l][i].p2;
            o["p3"] = layer[l][i].p3;
            la.append(o);
        }
        root[l == 0 ? "layerBase" : "layerFn"] = la;
    }
    QJsonArray ma;
    for (const Macro &m : macros) {
        QJsonObject o;
        o["name"] = m.name;
        QJsonArray aa;
        for (const MacroAction &a : m.actions) {
            QJsonObject ao;
            ao["delay"] = a.delayMs;
            ao["key"] = a.key;
            ao["flags"] = a.flags;
            aa.append(ao);
        }
        o["actions"] = aa;
        ma.append(o);
    }
    root["macros"] = ma;
    QJsonArray ra;
    for (uint8_t b : customRgb) ra.append(b);
    root["customRgb"] = ra;
    f.write(QJsonDocument(root).toJson());
    return true;
}

QVector<uint8_t> AppState::encodeMacroPayload(bool *ok) const {
    /* Legacy layout: 400-byte index (100 slots × u16 LE offset, 0 = empty),
       blocks from offset 400: [u16 len(actions*2), 0,0] then 4-byte actions. */
    QVector<uint8_t> out(400, 0);
    int off = 400;
    *ok = true;
    for (int i = 0; i < macros.size() && i < 100; i++) {
        const Macro &m = macros[i];
        if (m.actions.isEmpty()) continue;
        out[i * 4] = (uint8_t)(off & 0xff);
        out[i * 4 + 1] = (uint8_t)(off >> 8);
        int byteLen = 4 + m.actions.size() * 4;
        out.resize(off + byteLen);
        out[off] = (uint8_t)((m.actions.size() * 2) & 0xff);
        out[off + 1] = (uint8_t)((m.actions.size() * 2) >> 8);
        out[off + 2] = 0;
        out[off + 3] = 0;
        int p = off + 4;
        for (const MacroAction &a : m.actions) {
            out[p] = (uint8_t)(a.delayMs & 0xff);
            out[p + 1] = (uint8_t)(a.delayMs >> 8);
            out[p + 2] = a.key;
            out[p + 3] = a.flags;
            p += 4;
        }
        off += byteLen;
        if (off > 3072) { *ok = false; return out; } /* budget firmware 3072 o */
    }
    return out;
}

QString AppState::actionLabel(const KeyAction &a) {
    switch (a.page) {
    case 0x00: return QStringLiteral("Default");
    case 0x01: return QStringLiteral("Mouse");
    case 0x02: return QStringLiteral("Keyboard");
    case 0x03: return consumerKeyName(a.p1);
    case 0x05: return a.p1 == 3 ? QStringLiteral("Disabled") : QStringLiteral("Special");
    case 0x06: return QStringLiteral("Macro");
    case 0x07: return QStringLiteral("Extended");
    default: return QStringLiteral("Page %1").arg(a.page);
    }
}

} /* namespace ak820 */
