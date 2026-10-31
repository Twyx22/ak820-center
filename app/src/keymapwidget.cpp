#include "keymapwidget.h"
#include "keycodes.h"
#include "layout.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ak820 {

/* ---------- ActionDialog ---------- */

ActionDialog::ActionDialog(const KeyAction &current, int macroCount, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Assign key");
    setMinimumWidth(460);

    auto *type = new QComboBox;
    type->addItem("Default key", 0x00);
    type->addItem("Keyboard key (+ modifiers)", 0x02);
    type->addItem("Multimedia key", 0x03);
    type->addItem("Mouse action", 0x01);
    type->addItem("Disabled", 0x05);
    if (macroCount > 0)
        type->addItem("Macro", 0x06);

    auto *key = new QComboBox;
    for (int c = 0x04; c <= 0x73; c++)
        key->addItem(hidKeyName(c), c);
    for (int c = 0xE0; c <= 0xE7; c++)
        key->addItem(hidKeyName(c), c);

    auto *mod = new QComboBox;
    const struct { uint8_t bit; const char *n; } mods[] = {
        { 0x01, "Ctrl" }, { 0x02, "Shift" }, { 0x04, "Alt" }, { 0x08, "Win" },
        { 0x10, "Right Ctrl" }, { 0x20, "Right Shift" },
        { 0x40, "Right Alt" }, { 0x80, "Right Win" },
    };
    mod->addItem("None", 0);
    for (const auto &m : mods)
        mod->addItem(QString::fromUtf8(m.n), m.bit);

    auto *consumer = new QComboBox;
    const struct { uint8_t c; const char *n; } cons[] = {
        { 0xB5, "Next track" }, { 0xB6, "Previous track" }, { 0xB7, "Stop" },
        { 0xCD, "Play / Pause" }, { 0xE2, "Mute" }, { 0xE9, "Volume up" },
        { 0xEA, "Volume down" }, { 0x92, "Calculator" }, { 0x9C, "Mail" },
        { 0x83, "Sleep" },
    };
    for (const auto &c : cons)
        consumer->addItem(QString::fromUtf8(c.n), c.c);

    auto *mouse = new QComboBox;
    const struct { uint8_t p1, p2; const char *n; } mbtns[] = {
        { 0x01, 0x01, "Left click" },
        { 0x01, 0x02, "Right click" },
        { 0x01, 0x04, "Middle click" },
        { 0x03, 0x01, "Scroll up" },
        { 0x03, 0xFF, "Scroll down" },
        { 0x01, 0x08, "Mouse button 4" },
        { 0x01, 0x10, "Mouse button 5" },
    };
    for (const auto &m : mbtns)
        mouse->addItem(QString::fromUtf8(m.n), (m.p1 << 8) | m.p2);

    auto *macro = new QComboBox;
    const auto &macros = AppState::instance()->macros;
    for (int i = 0; i < macros.size(); i++)
        macro->addItem(macros[i].name, i);

    auto *stack = new QStackedWidget;
    auto mkPage = [](QWidget *w) { auto *l = new QVBoxLayout; l->addWidget(w); l->addStretch(1);
                                   QWidget *p = new QWidget; p->setLayout(l); return p; };
    stack->insertWidget(0x00, mkPage(new QLabel("The key keeps its factory function.")));
    stack->insertWidget(0x02, mkPage(key));
    stack->insertWidget(0x03, mkPage(consumer));
    stack->insertWidget(0x01, mkPage(mouse));
    stack->insertWidget(0x05, mkPage(new QLabel("The key will do nothing.")));
    stack->insertWidget(0x06, mkPage(macro));

    const int pageToStack[] = { 0, -1, 1, 2, -1, 3, 4, -1, 5 };
    connect(type, &QComboBox::currentIndexChanged, this, [=](int idx) {
        int page = type->itemData(idx).toInt();
        if (page >= 0 && page < 9 && pageToStack[page] >= 0)
            stack->setCurrentIndex(pageToStack[page]);
    });

    int cur = current.page;
    if (cur != 0x00 && cur != 0x01 && cur != 0x02 && cur != 0x03 && cur != 0x05 && cur != 0x06)
        cur = 0x00;
    int tIdx = type->findData(cur);
    type->setCurrentIndex(tIdx < 0 ? 0 : tIdx);

    switch (current.page) {
    case 0x02:
        key->setCurrentIndex(key->findData(current.p2));
        mod->setCurrentIndex(mod->findData(current.p1));
        break;
    case 0x03: consumer->setCurrentIndex(consumer->findData(current.p1)); break;
    case 0x01: mouse->setCurrentIndex(mouse->findData((current.p1 << 8) | current.p2)); break;
    case 0x06: macro->setCurrentIndex(current.p1); break;
    default: break;
    }

    auto *form = new QFormLayout;
    form->setSpacing(10);
    form->addRow("Assignment type", type);
    form->addRow("Modifier (keyboard)", mod);
    form->addRow(QString(), stack);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(btns);

    connect(this, &QDialog::accepted, this, [=]() {
        int page = type->currentData().toInt();
        m_result.page = (uint8_t)page;
        m_result.p1 = m_result.p2 = m_result.p3 = 0;
        switch (page) {
        case 0x02: {
            int k = key->currentData().toInt(), m = mod->currentData().toInt();
            if (k >= 0xE0) { /* bare modifier */
                static const uint8_t conv[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
                m_result.p1 = conv[k - 0xE0];
                m_result.p2 = 0;
            } else {
                m_result.p1 = (uint8_t)m;
                m_result.p2 = (uint8_t)k;
            }
            break;
        }
        case 0x03: m_result.p1 = (uint8_t)consumer->currentData().toInt(); break;
        case 0x01: {
            int v = mouse->currentData().toInt();
            m_result.p1 = (uint8_t)(v >> 8);
            m_result.p2 = (uint8_t)(v & 0xff);
            break;
        }
        case 0x06:
            m_result.p1 = (uint8_t)macro->currentData().toInt();
            m_result.p2 = 0; /* play once */
            m_result.p3 = 0;
            break;
        default: break;
        }
    });
}

/* ---------- KeymapWidget ---------- */

KeymapWidget::KeymapWidget(QWidget *parent) : QWidget(parent) {
    m_view = new KeyboardView;
    m_view->setLayout(Layout::instance());
    m_view->setMode(KeyboardView::ModeRemap);

    m_layerBase = new QPushButton("Base layer");
    m_layerFn = new QPushButton("Fn layer");
    m_layerBase->setCheckable(true);
    m_layerFn->setCheckable(true);

    m_apply = new QPushButton("Write layer to keyboard");
    m_apply->setObjectName("accent");
    m_reset = new QPushButton("Reset all to default");
    m_hint = new QLabel("Click a key to assign an action. Base layer = normal keys, Fn layer = hold Fn.");
    m_hint->setObjectName("hint");
    m_status = new QLabel;

    auto *layLay = new QHBoxLayout;
    layLay->addWidget(m_layerBase);
    layLay->addWidget(m_layerFn);
    layLay->addStretch(1);

    auto *btnLay = new QHBoxLayout;
    btnLay->addWidget(m_apply);
    btnLay->addWidget(m_reset);
    btnLay->addStretch(1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    root->addLayout(layLay);
    root->addWidget(m_view, 1);
    root->addWidget(m_hint);
    root->addLayout(btnLay);
    root->addWidget(m_status);

    connect(m_view, &KeyboardView::slotActivated, this, &KeymapWidget::keyClicked);
    connect(m_layerBase, &QPushButton::clicked, this, [this]() { switchLayer(0); });
    connect(m_layerFn, &QPushButton::clicked, this, [this]() { switchLayer(1); });
    connect(m_apply, &QPushButton::clicked, this, &KeymapWidget::applyLayer);
    connect(m_reset, &QPushButton::clicked, this, &KeymapWidget::resetLayer);
    switchLayer(0);
}

void KeymapWidget::switchLayer(int layer) {
    m_layer = layer;
    m_layerBase->setChecked(layer == 0);
    m_layerFn->setChecked(layer == 1);
    refreshActions();
}

void KeymapWidget::refreshActions() {
    m_view->clearActions();
    const auto &l = AppState::instance()->layer[m_layer];
    for (int i = 0; i < LED_COUNT; i++) {
        const KeyAction &a = l[i];
        if (a.page == 0x00) continue;
        QString lbl = AppState::actionLabel(a);
        if (a.page == 0x02) {
            lbl.clear();
            if (a.p1) { /* HID modifier mask: 01=Ctrl 02=Shift 04=Alt 08=Win */
                if (a.p1 & 0x01) lbl += "Ctrl+";
                if (a.p1 & 0x02) lbl += "Shift+";
                if (a.p1 & 0x04) lbl += "Alt+";
                if (a.p1 & 0x08) lbl += "Win+";
            }
            lbl += hidKeyName(a.p2 ? a.p2 : a.p1);
        } else if (a.page == 0x06) {
            const auto &macros = AppState::instance()->macros;
            lbl = a.p1 < macros.size() ? macros[a.p1].name : QString("Macro %1").arg(a.p1);
        }
        m_view->setActionForSlot(i, lbl);
    }
}

void KeymapWidget::keyClicked(int lightIndex) {
    ActionDialog dlg(AppState::instance()->layer[m_layer][lightIndex],
                     AppState::instance()->macros.size(), this);
    if (dlg.exec() == QDialog::Accepted) {
        AppState::instance()->layer[m_layer][lightIndex] = dlg.result();
        AppState::instance()->saveState();
        refreshActions();
    }
}

void KeymapWidget::applyLayer() {
    if (Device::instance()->writeKeymapLayer(m_layer, AppState::instance()->layer[m_layer]))
        m_status->setText(QString("Layer %1 written.").arg(m_layer == 0 ? "base" : "Fn"));
    else
        m_status->setText(QString("Error: %1").arg(Device::instance()->lastError()));
    m_status->setStyleSheet(m_status->text().startsWith("Layer")
                                ? "color:#7ec97e;" : "color:#e07070;");
}

void KeymapWidget::resetLayer() {
    AppState::instance()->layer[m_layer].fill(KeyAction{});
    AppState::instance()->saveState();
    refreshActions();
}

} /* namespace ak820 */
