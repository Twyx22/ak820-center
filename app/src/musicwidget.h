/*
 * musicwidget.h — "Music Rhythm" tab: audio-reactive real-time lighting.
 *
 * EXPERIMENTAL: streams 144x3 RGB frames to the keyboard through the 2.4G
 * dongle T2 transport (Device::pushRealtimeFrame, see device.h). The whole
 * engine (audio capture, FFT, palettes, HID throttling) lives in
 * musicwidget.cpp as private helpers; only the widget itself is exposed here.
 */
#pragma once

#include <QWidget>

namespace ak820 {

class MusicWidget : public QWidget {
    Q_OBJECT
public:
    explicit MusicWidget(QWidget *parent = nullptr);
    ~MusicWidget() override;
};

} /* namespace ak820 */
