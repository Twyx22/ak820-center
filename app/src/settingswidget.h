/*
 * settingswidget.h — connection, auto-sleep, clock sync, battery.
 */
#pragma once

#include "device.h"
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

namespace ak820 {

class SettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWidget(QWidget *parent = nullptr);

private slots:
    void refreshConnection();
    void applySleep();
    void syncClockNow();
    void pollBattery();

private:
    QComboBox *m_sleep = nullptr;
    QPushButton *m_sleepBtn = nullptr;
    QPushButton *m_clockBtn = nullptr;
    QLabel *m_batt = nullptr;
    QLabel *m_conn = nullptr;
    QLabel *m_status = nullptr;
    QTimer m_battTimer;
};

} /* namespace ak820 */
