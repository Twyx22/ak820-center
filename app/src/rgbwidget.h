/*
 * rgbwidget.h — per-key custom RGB painting.
 */
#pragma once

#include "appstate.h"
#include "keyboardview.h"
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>

namespace ak820 {

class RgbWidget : public QWidget {
    Q_OBJECT
public:
    explicit RgbWidget(QWidget *parent = nullptr);

private slots:
    void keyClicked(int lightIndex);
    void apply();
    void applyMode();
    void paintAll();
    void loadFromKeyboard();

private:
    void refreshColors();

    KeyboardView *m_view = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_modeBtn = nullptr;
    QPushButton *m_fillBtn = nullptr;
    QPushButton *m_loadBtn = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    QColor m_color{255, 60, 60};
    QSlider *m_bright = nullptr;
    class ColorPreview *m_preview = nullptr;
};

} /* namespace ak820 */
