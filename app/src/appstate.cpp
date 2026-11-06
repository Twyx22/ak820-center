#include "appstate.h"
#include "keycodes.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace ak820 {

namespace {

/* --- JSON helpers shared by state.json and profile export/import. ------- */

QJsonObject lightingToJson(const LightingConfig &c) {
    QJsonObject o;
    o["mode"] = c.mode;
    o["r"] = c.r;
    o["g"] = c.g;
    o["b"] = c.b;
    o["colorful"] = c.colorful;
    o["brightness"] = c.brightness;
    o["speed"] = c.speed;
    o["direction"] = c.direction;
    return o;
}

/* Tolerant field-wise parse; only requires an object. Missing fields keep
   their current values. */
bool lightingFromJson(const QJsonValue &v, LightingConfig &c) {
    if (!v.isObject()) return false;
    QJsonObject o = v.toObject();
    c.mode = (uint8_t)o.value("mode").toInt(c.mode);
    c.r = (uint8_t)o.value("r").toInt(c.r);
    c.g = (uint8_t)o.value("g").toInt(c.g);
    c.b = (uint8_t)o.value("b").toInt(c.b);
    c.colorful = (uint8_t)o.value("colorful").toInt(c.colorful);
    c.brightness = (uint8_t)o.value("brightness").toInt(c.brightness);
    c.speed = (uint8_t)o.value("speed").toInt(c.speed);
    c.direction = (uint8_t)o.value("direction").toInt(c.direction);
    return true;
}

QJsonArray layerToJson(const std::array<KeyAction, LED_COUNT> &layer) {
    QJsonArray la;
    for (int i = 0; i < LED_COUNT; i++) {
        QJsonObject o;
        o["page"] = layer[i].page;
        o["p1"] = layer[i].p1;
        o["p2"] = layer[i].p2;
        o["p3"] = layer[i].p3;
        la.append(o);
    }
    return la;
}

/* With strictSize the array must hold exactly LED_COUNT entries (profile
   import); otherwise extra entries are ignored and missing ones keep their
   current values (state.json load). */
bool layerFromJson(const QJsonValue &v, std::array<KeyAction, LED_COUNT> &layer,
                   bool strictSize) {
    QJsonArray la = v.toArray();
    if (la.isEmpty() || (strictSize && la.size() != LED_COUNT)) return false;
    for (int i = 0; i < la.size() && i < LED_COUNT; i++) {
        QJsonObject o = la[i].toObject();
        layer[i].page = (uint8_t)o.value("page").toInt();
        layer[i].p1 = (uint8_t)o.value("p1").toInt();
        layer[i].p2 = (uint8_t)o.value("p2").toInt();
        layer[i].p3 = (uint8_t)o.value("p3").toInt();
    }
    return true;
}

QJsonArray macrosToJson(const QVector<Macro> &macros) {
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
    return ma;
}

bool macrosFromJson(const QJsonValue &v, QVector<Macro> &macros) {
    QJsonArray ma = v.toArray();
    if (ma.isEmpty() && !v.isArray()) return false;
    macros.clear();
    for (const auto &mv : ma) {
        QJsonObject o = mv.toObject();
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
    return true;
}

QJsonArray rgbToJson(const std::array<uint8_t, LED_COUNT * 3> &rgb) {
    QJsonArray ra;
    for (uint8_t b : rgb) ra.append(b);
    return ra;
}

/* strictSize requires exactly LED_COUNT * 3 entries (profile import). */
bool rgbFromJson(const QJsonValue &v, std::array<uint8_t, LED_COUNT * 3> &rgb,
                 bool strictSize) {
    QJsonArray ra = v.toArray();
    if (ra.isEmpty() || (strictSize && ra.size() != (int)rgb.size())) return false;
    for (int i = 0; i < ra.size() && i < (int)rgb.size(); i++)
        rgb[i] = (uint8_t)ra[i].toInt();
    return true;
}

/* Export schema: the profile fields wrapped in a type/version envelope. */
QJsonObject profileEnvelope(const Profile &p) {
    QJsonObject o;
    o["type"] = QStringLiteral("ak820-center-profile");
    o["version"] = 1;
    o["name"] = p.name;
    o["lighting"] = lightingToJson(p.lighting);
    o["layerBase"] = layerToJson(p.layerBase);
    o["layerFn"] = layerToJson(p.layerFn);
    o["macros"] = macrosToJson(p.macros);
    o["customRgb"] = rgbToJson(p.customRgb);
    o["tftSlot"] = p.tftSlot;
    return o;
}

} /* namespace */

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

    layerFromJson(root.value("layerBase"), layer[0], /*strictSize=*/false);
    layerFromJson(root.value("layerFn"), layer[1], /*strictSize=*/false);
    macrosFromJson(root.value("macros"), macros);
    rgbFromJson(root.value("customRgb"), customRgb, /*strictSize=*/false);
    /* Absent key keeps the default config. */
    lightingFromJson(root.value("lighting"), lighting);

    profiles.clear();
    for (const auto &pv : root.value("profiles").toArray()) {
        QJsonObject po = pv.toObject();
        Profile p;
        p.name = po.value("name").toString();
        lightingFromJson(po.value("lighting"), p.lighting);
        layerFromJson(po.value("layerBase"), p.layerBase, /*strictSize=*/false);
        layerFromJson(po.value("layerFn"), p.layerFn, /*strictSize=*/false);
        macrosFromJson(po.value("macros"), p.macros);
        rgbFromJson(po.value("customRgb"), p.customRgb, /*strictSize=*/false);
        p.tftSlot = po.value("tftSlot").toInt(p.tftSlot);
        profiles.append(p);
    }
    return true;
}

bool AppState::saveState() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QFile f(statePath());
    if (!f.open(QIODevice::WriteOnly)) return false;

    QJsonObject root;
    root["layerBase"] = layerToJson(layer[0]);
    root["layerFn"] = layerToJson(layer[1]);
    root["macros"] = macrosToJson(macros);
    root["customRgb"] = rgbToJson(customRgb);
    root["lighting"] = lightingToJson(lighting);

    QJsonArray pa;
    for (const Profile &p : profiles) {
        QJsonObject po;
        po["name"] = p.name;
        po["lighting"] = lightingToJson(p.lighting);
        po["layerBase"] = layerToJson(p.layerBase);
        po["layerFn"] = layerToJson(p.layerFn);
        po["macros"] = macrosToJson(p.macros);
        po["customRgb"] = rgbToJson(p.customRgb);
        po["tftSlot"] = p.tftSlot;
        pa.append(po);
    }
    root["profiles"] = pa;
    f.write(QJsonDocument(root).toJson());
    return true;
}

void AppState::setLightingConfig(const LightingConfig &c) {
    lighting = c;
    emit lightingChanged();
    saveState();
}

int AppState::saveProfile(const QString &name) {
    Profile p;
    p.name = name;
    p.lighting = lighting;
    p.layerBase = layer[0];
    p.layerFn = layer[1];
    p.macros = macros;
    p.customRgb = customRgb;
    profiles.append(p);
    saveState();
    emit profilesChanged();
    return profiles.size() - 1;
}

bool AppState::applyProfile(int index) {
    if (index < 0 || index >= profiles.size()) return false;
    const Profile &p = profiles[index];
    lighting = p.lighting;
    layer[0] = p.layerBase;
    layer[1] = p.layerFn;
    macros = p.macros;
    customRgb = p.customRgb;
    emit lightingChanged();
    emit layersChanged();
    emit macrosChanged();
    emit rgbChanged();
    saveState();
    return true;
}

bool AppState::deleteProfile(int index) {
    if (index < 0 || index >= profiles.size()) return false;
    profiles.removeAt(index);
    saveState();
    emit profilesChanged();
    return true;
}

bool AppState::exportProfile(int index, const QString &path) const {
    if (index < 0 || index >= profiles.size()) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(profileEnvelope(profiles[index])).toJson());
    return true;
}

int AppState::importProfile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return -1;
    QJsonObject root = doc.object();

    /* Schema validation: type, version, and exact array sizes. */
    if (root.value("type").toString() != QLatin1String("ak820-center-profile"))
        return -1;
    if (root.value("version").toInt() != 1) return -1;
    Profile p;
    if (!lightingFromJson(root.value("lighting"), p.lighting)) return -1;
    if (!layerFromJson(root.value("layerBase"), p.layerBase, /*strictSize=*/true))
        return -1;
    if (!layerFromJson(root.value("layerFn"), p.layerFn, /*strictSize=*/true))
        return -1;
    if (!macrosFromJson(root.value("macros"), p.macros)) return -1;
    if (!rgbFromJson(root.value("customRgb"), p.customRgb, /*strictSize=*/true))
        return -1;
    p.name = root.value("name").toString();
    if (p.name.isEmpty()) p.name = QStringLiteral("Imported profile");
    p.tftSlot = root.value("tftSlot").toInt(p.tftSlot);

    profiles.append(p);
    saveState();
    emit profilesChanged();
    return profiles.size() - 1;
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
        if (off > 3072) { *ok = false; return out; } /* firmware budget 3072 bytes */
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
