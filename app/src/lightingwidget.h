/*
 * lightingwidget.h — Lighting tab: full firmware effect list, color,
 * rainbow type, brightness, speed, direction.
 */
#pragma once

#include "device.h"
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

namespace ak820 {

class LightingWidget : public QWidget {
    Q_OBJECT
public:
    explicit LightingWidget(QWidget *parent = nullptr);

private slots:
    void apply();
    void pickColor();

private:
    LightingConfig currentConfig() const;
    void setControls(const LightingConfig &cfg);

    QComboBox *m_mode = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QComboBox *m_colorful = nullptr;
    QSlider *m_bright = nullptr;
    QSlider *m_speed = nullptr;
    QSlider *m_dir = nullptr;
    QPushButton *m_apply = nullptr;
    QLabel *m_status = nullptr;
    QColor m_color{0, 120, 255};
    QWidget *m_preview = nullptr;
};

} /* namespace ak820 */
