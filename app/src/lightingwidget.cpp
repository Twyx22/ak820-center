#include "lightingwidget.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace ak820 {

struct ModeDef { const char *label; uint8_t id; };
/* Full firmware list — language keys 521-540 of the official driver
   (also used by the real-time list FUN_0042b3b0, ids 1..19). */
static const ModeDef kModes[] = {
    { "Static", 0x01 },
    { "Single On", 0x02 },
    { "Single Off", 0x03 },
    { "Glittering", 0x04 },
    { "Falling", 0x05 },
    { "Colourful", 0x06 },
    { "Breath", 0x07 },
    { "Spectrum", 0x08 },
    { "Outward", 0x09 },
    { "Scrolling", 0x0A },
    { "Rolling", 0x0B },
    { "Rotating", 0x0C },
    { "Explode", 0x0D },
    { "Launch", 0x0E },
    { "Ripples", 0x0F },
    { "Flowing", 0x10 },
    { "Pulsating", 0x11 },
    { "Tilt", 0x12 },
    { "Shuttle", 0x13 },
    { "Off (LED Off)", ModeOff },                  /* 0x00 */
    { "Custom (per-key)", ModeCustom },            /* 0x80 */
};

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

LightingWidget::LightingWidget(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *fxBox = new QGroupBox("Effect");
    auto *fx = new QFormLayout(fxBox);
    fx->setSpacing(10);
    m_mode = new QComboBox;
    for (const ModeDef &m : kModes)
        m_mode->addItem(QString::fromUtf8(m.label), m.id);
    m_mode->setCurrentIndex(0); /* static */
    fx->addRow("Mode", m_mode);

    m_colorful = new QComboBox;
    m_colorful->addItem("Fixed color", 0);
    m_colorful->addItem("Rainbow", 1);
    m_colorful->addItem("Rainbow (reversed)", 2);
    fx->addRow("Color mode", m_colorful);

    auto *colorRow = new QWidget;
    auto *colorLay = new QHBoxLayout(colorRow);
    colorLay->setContentsMargins(0, 0, 0, 0);
    m_preview = new ColorPreview(&m_color);
    m_colorBtn = new QPushButton("Pick color…");
    colorLay->addWidget(m_preview);
    colorLay->addWidget(m_colorBtn);
    colorLay->addStretch(1);
    fx->addRow("Color", colorRow);
    root->addWidget(fxBox);

    auto *parBox = new QGroupBox("Parameters");
    auto *par = new QFormLayout(parBox);
    par->setSpacing(10);
    m_bright = new QSlider(Qt::Horizontal);
    m_bright->setRange(0, 5); m_bright->setValue(5);
    m_bright->setTickPosition(QSlider::TicksBelow);
    par->addRow("Brightness (0-5)", m_bright);

    m_speed = new QSlider(Qt::Horizontal);
    m_speed->setRange(0, 5); m_speed->setValue(3);
    m_speed->setTickPosition(QSlider::TicksBelow);
    par->addRow("Speed (0-5)", m_speed);

    m_dir = new QSlider(Qt::Horizontal);
    m_dir->setRange(0, 3); m_dir->setValue(0);
    m_dir->setPageStep(1);
    par->addRow("Direction (Left/Down/Up/Right)", m_dir);
    root->addWidget(parBox);

    auto *bottom = new QHBoxLayout;
    m_apply = new QPushButton("Apply to keyboard");
    m_apply->setObjectName("accent");
    m_apply->setMinimumHeight(38);
    m_apply->setMinimumWidth(200);
    bottom->addStretch(1);
    bottom->addWidget(m_apply);
    bottom->addStretch(1);
    root->addLayout(bottom);

    m_status = new QLabel;
    m_status->setAlignment(Qt::AlignCenter);
    root->addWidget(m_status);
    root->addStretch(1);

    connect(m_colorBtn, &QPushButton::clicked, this, &LightingWidget::pickColor);
    connect(m_apply, &QPushButton::clicked, this, &LightingWidget::apply);
}

void LightingWidget::pickColor() {
    QColor c = QColorDialog::getColor(m_color, this, "Backlight color");
    if (c.isValid()) {
        m_color = c;
        m_preview->update();
    }
}

void LightingWidget::apply() {
    LightingConfig cfg;
    cfg.mode = (uint8_t)m_mode->currentData().toInt();
    cfg.r = m_color.red(); cfg.g = m_color.green(); cfg.b = m_color.blue();
    cfg.colorful = (uint8_t)m_colorful->currentData().toInt();
    cfg.brightness = (uint8_t)m_bright->value();
    cfg.speed = (uint8_t)m_speed->value();
    cfg.direction = (uint8_t)m_dir->value();
    if (Device::instance()->setLighting(cfg))
        m_status->setText("Applied.");
    else
        m_status->setText(QString("Error: %1").arg(Device::instance()->lastError()));
    m_status->setObjectName(m_status->text().startsWith("Applied") ? "statusOk" : "statusErr");
    m_status->setStyleSheet(m_status->objectName() == "statusOk"
                                ? "color:#7ec97e;" : "color:#e07070;");
}

} /* namespace ak820 */
