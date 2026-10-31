#include "rgbwidget.h"
#include "layout.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace ak820 {

class ColorPreview : public QWidget {
public:
    ColorPreview(QColor *c, QWidget *parent = nullptr) : QWidget(parent), m_c(c) {
        setFixedSize(56, 26);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(80, 80, 90), 1));
        p.setBrush(*m_c);
        p.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 4, 4);
    }
private:
    QColor *m_c;
};

RgbWidget::RgbWidget(QWidget *parent) : QWidget(parent) {
    m_view = new KeyboardView;
    m_view->setLayout(Layout::instance());
    m_view->setMode(KeyboardView::ModeRgb);

    auto *pick = new QPushButton("Brush color…");
    m_preview = new ColorPreview(&m_color);
    m_fillBtn = new QPushButton("Fill all keys");
    m_modeBtn = new QPushButton("Enable custom mode");
    m_apply = new QPushButton("Send painting to keyboard");
    m_apply->setObjectName("accent");
    m_loadBtn = new QPushButton("Read from keyboard");
    m_hint = new QLabel("Click keys to paint them with the brush color.");
    m_hint->setObjectName("hint");
    m_status = new QLabel;

    m_bright = new QSlider(Qt::Horizontal);
    m_bright->setRange(0, 5);
    m_bright->setValue(5);
    m_bright->setFixedWidth(160);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(m_preview);
    row1->addWidget(pick);
    row1->addWidget(m_fillBtn);
    row1->addStretch(1);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel("Custom mode brightness:"));
    row2->addWidget(m_bright);
    row2->addWidget(m_modeBtn);
    row2->addStretch(1);

    auto *row3 = new QHBoxLayout;
    row3->addWidget(m_apply);
    row3->addWidget(m_loadBtn);
    row3->addStretch(1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    root->addLayout(row1);
    root->addLayout(row2);
    root->addLayout(row3);
    root->addWidget(m_view, 1);
    root->addWidget(m_hint);
    root->addWidget(m_status);

    refreshColors();
    connect(pick, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_color, this, "Brush color");
        if (c.isValid()) { m_color = c; m_preview->update(); }
    });
    connect(m_fillBtn, &QPushButton::clicked, this, &RgbWidget::paintAll);
    connect(m_modeBtn, &QPushButton::clicked, this, &RgbWidget::applyMode);
    connect(m_apply, &QPushButton::clicked, this, &RgbWidget::apply);
    connect(m_loadBtn, &QPushButton::clicked, this, &RgbWidget::loadFromKeyboard);
    connect(m_view, &KeyboardView::slotActivated, this, &RgbWidget::keyClicked);
}

void RgbWidget::refreshColors() {
    m_view->clearColors();
    const auto &rgb = AppState::instance()->customRgb;
    for (int i = 0; i < LED_COUNT; i++)
        m_view->setColorForSlot(i, QColor(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]));
}

void RgbWidget::keyClicked(int lightIndex) {
    auto &rgb = AppState::instance()->customRgb;
    rgb[lightIndex * 3 + 0] = m_color.red();
    rgb[lightIndex * 3 + 1] = m_color.green();
    rgb[lightIndex * 3 + 2] = m_color.blue();
    AppState::instance()->saveState();
    refreshColors();
}

void RgbWidget::paintAll() {
    auto &rgb = AppState::instance()->customRgb;
    for (int i = 0; i < LED_COUNT; i++) {
        rgb[i * 3 + 0] = m_color.red();
        rgb[i * 3 + 1] = m_color.green();
        rgb[i * 3 + 2] = m_color.blue();
    }
    AppState::instance()->saveState();
    refreshColors();
}

void RgbWidget::applyMode() {
    /* Mode 0x80 (custom) via cmd 0x13: packet [80 … bright … AA 55], flag 0. */
    Device *d = Device::instance();
    Packet mode;
    mode.fill(0);
    mode[0] = ModeCustom;
    mode[9] = (uint8_t)m_bright->value();
    mode[14] = 0xAA; mode[15] = 0x55;
    if (!d->isConnected() && !d->connectKeyboard()) {
        m_status->setText(QString("Error: %1").arg(d->lastError()));
        m_status->setStyleSheet("color:#e07070;");
        return;
    }
    if (d->writeSetting(0x13, 0, 1, &mode)) {
        m_status->setText("Custom mode enabled.");
        m_status->setStyleSheet("color:#7ec97e;");
    } else {
        m_status->setText(QString("Error: %1").arg(d->lastError()));
        m_status->setStyleSheet("color:#e07070;");
    }
}

void RgbWidget::apply() {
    if (Device::instance()->writeCustomRgb(AppState::instance()->customRgb)) {
        m_status->setText("Painting sent.");
        m_status->setStyleSheet("color:#7ec97e;");
    } else {
        m_status->setText(QString("Error: %1").arg(Device::instance()->lastError()));
        m_status->setStyleSheet("color:#e07070;");
    }
}

void RgbWidget::loadFromKeyboard() {
    std::array<uint8_t, LED_COUNT * 3> rgb{};
    if (Device::instance()->readCustomRgb(rgb)) {
        AppState::instance()->customRgb = rgb;
        AppState::instance()->saveState();
        refreshColors();
        m_status->setText("Color bank read from keyboard.");
        m_status->setStyleSheet("color:#7ec97e;");
    } else {
        m_status->setText(QString("Error: %1").arg(Device::instance()->lastError()));
        m_status->setStyleSheet("color:#e07070;");
    }
}

} /* namespace ak820 */
