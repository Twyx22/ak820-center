#include "rgbwidget.h"

#include "device.h"
#include "hidworker.h"
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

    auto *pick = new QPushButton(tr("Brush color…"));
    m_preview = new ColorPreview(&m_color);
    m_fillBtn = new QPushButton(tr("Fill all keys"));
    m_modeBtn = new QPushButton(tr("Enable custom mode"));
    m_apply = new QPushButton(tr("Send painting to keyboard"));
    m_apply->setObjectName("accent");
    m_loadBtn = new QPushButton(tr("Read from keyboard"));
    m_liveChk = new QCheckBox(tr("Live preview (experimental — needs 2.4G dongle)"));
    m_hint = new QLabel(tr("Click keys to paint them with the brush color."));
    m_hint->setObjectName("hint");
    m_status = new QLabel;

    m_bright = new QSlider(Qt::Horizontal);
    m_bright->setRange(0, 5);
    m_bright->setValue(5);
    m_bright->setFixedWidth(160);

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(150);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(m_preview);
    row1->addWidget(pick);
    row1->addWidget(m_fillBtn);
    row1->addWidget(m_liveChk);
    row1->addStretch(1);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(tr("Custom mode brightness:")));
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
        QColor c = QColorDialog::getColor(m_color, this, tr("Brush color"));
        if (c.isValid()) { m_color = c; m_preview->update(); }
    });
    connect(m_fillBtn, &QPushButton::clicked, this, &RgbWidget::paintAll);
    connect(m_modeBtn, &QPushButton::clicked, this, &RgbWidget::applyMode);
    connect(m_apply, &QPushButton::clicked, this, &RgbWidget::apply);
    connect(m_loadBtn, &QPushButton::clicked, this, &RgbWidget::loadFromKeyboard);
    connect(m_view, &KeyboardView::slotActivated, this, &RgbWidget::keyClicked);
    connect(&m_previewTimer, &QTimer::timeout, this, [this]() {
        /* Fire-and-forget frame push; failures surface once, below. */
        HidWorker::run(this, [](Device *dev) {
            return dev->pushRealtimeFrame(AppState::instance()->customRgb.data());
        }, [this](bool ok, QString err) {
            if (ok) {
                m_liveWarned = false;
            } else if (!m_liveWarned) {
                m_liveWarned = true;
                m_status->setText(tr("Live preview unavailable: %1 "
                                     "(experimental — requires the 2.4G dongle)").arg(err));
                m_status->setStyleSheet(QStringLiteral("color:#e07070;"));
            }
        });
    });
}

void RgbWidget::refreshColors() {
    m_view->clearColors();
    const auto &rgb = AppState::instance()->customRgb;
    for (int i = 0; i < LED_COUNT; i++)
        m_view->setColorForSlot(i, QColor(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]));
}

void RgbWidget::schedulePreview() {
    if (m_liveChk->isChecked())
        m_previewTimer.start();
}

void RgbWidget::keyClicked(int lightIndex) {
    auto &rgb = AppState::instance()->customRgb;
    rgb[lightIndex * 3 + 0] = m_color.red();
    rgb[lightIndex * 3 + 1] = m_color.green();
    rgb[lightIndex * 3 + 2] = m_color.blue();
    AppState::instance()->saveState();
    refreshColors();
    schedulePreview();
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
    schedulePreview();
}

void RgbWidget::applyMode() {
    /* Mode 0x80 (custom) via cmd 0x13: packet [80 … bright … AA 55], flag 0. */
    Packet mode;
    mode.fill(0);
    mode[0] = ModeCustom;
    mode[9] = (uint8_t)m_bright->value();
    mode[14] = 0xAA; mode[15] = 0x55;

    m_modeBtn->setEnabled(false);
    m_status->setText(tr("Enabling custom mode…"));
    m_status->setStyleSheet(QStringLiteral("color:#8a8a94;"));
    HidWorker::run(this, [mode](Device *dev) {
        if (!dev->isConnected() && !dev->connectKeyboard())
            return false;
        return dev->writeSetting(0x13, 0, 1, &mode);
    }, [this](bool ok, QString err) {
        m_modeBtn->setEnabled(true);
        if (ok) {
            m_status->setText(tr("Custom mode enabled."));
            m_status->setStyleSheet(QStringLiteral("color:#7ec97e;"));
        } else {
            m_status->setText(tr("Error: %1").arg(err));
            m_status->setStyleSheet(QStringLiteral("color:#e07070;"));
        }
    });
}

void RgbWidget::apply() {
    /* Copy the bank: the reference must stay valid on the HID thread. */
    const auto rgb = AppState::instance()->customRgb;

    m_apply->setEnabled(false);
    m_status->setText(tr("Sending painting…"));
    m_status->setStyleSheet(QStringLiteral("color:#8a8a94;"));
    HidWorker::run(this, [rgb](Device *dev) { return dev->writeCustomRgb(rgb); },
                   [this](bool ok, QString err) {
        m_apply->setEnabled(true);
        if (ok) {
            m_status->setText(tr("Painting sent."));
            m_status->setStyleSheet(QStringLiteral("color:#7ec97e;"));
        } else {
            m_status->setText(tr("Error: %1").arg(err));
            m_status->setStyleSheet(QStringLiteral("color:#e07070;"));
        }
    });
}

void RgbWidget::loadFromKeyboard() {
    m_loadBtn->setEnabled(false);
    m_status->setText(tr("Reading color bank…"));
    m_status->setStyleSheet(QStringLiteral("color:#8a8a94;"));
    HidWorker::run(this, [this](Device *dev) { return dev->readCustomRgb(m_readBuf); },
                   [this](bool ok, QString err) {
        m_loadBtn->setEnabled(true);
        if (ok) {
            AppState::instance()->customRgb = m_readBuf;
            AppState::instance()->saveState();
            refreshColors();
            m_status->setText(tr("Color bank read from keyboard."));
            m_status->setStyleSheet(QStringLiteral("color:#7ec97e;"));
        } else {
            m_status->setText(tr("Error: %1").arg(err));
            m_status->setStyleSheet(QStringLiteral("color:#e07070;"));
        }
    });
}

} /* namespace ak820 */
