/*
 * rgbwidget.h — per-key custom RGB painting.
 */
#pragma once

#include "appstate.h"
#include "keyboardview.h"
#include <QWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QTimer>

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
    void schedulePreview();

    KeyboardView *m_view = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_modeBtn = nullptr;
    QPushButton *m_fillBtn = nullptr;
    QPushButton *m_loadBtn = nullptr;
    QCheckBox *m_liveChk = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    QColor m_color{255, 60, 60};
    QSlider *m_bright = nullptr;
    class ColorPreview *m_preview = nullptr;
    QTimer m_previewTimer;
    std::array<uint8_t, LED_COUNT * 3> m_readBuf{};
    bool m_liveWarned = false;
};

} /* namespace ak820 */
