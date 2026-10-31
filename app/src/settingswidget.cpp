#include "settingswidget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace ak820 {

SettingsWidget::SettingsWidget(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *connBox = new QGroupBox("Connection");
    m_conn = new QLabel("Not connected");
    m_conn->setObjectName("hint");
    auto *connLay = new QVBoxLayout(connBox);
    connLay->addWidget(m_conn);
    auto *usbOnly = new QLabel("This app configures the keyboard over wired USB only. "
                               "The 2.4G dongle and Bluetooth are not supported.");
    usbOnly->setObjectName("hint");
    usbOnly->setWordWrap(true);
    connLay->addWidget(usbOnly);
    root->addWidget(connBox);

    auto *powerBox = new QGroupBox("Auto sleep");
    auto *power = new QFormLayout(powerBox);
    power->setSpacing(10);
    m_sleep = new QComboBox;
    m_sleep->addItem("Never", 0);
    m_sleep->addItem("1 minute", 1);
    m_sleep->addItem("5 minutes", 2);
    m_sleep->addItem("30 minutes", 3);
    m_sleep->setCurrentIndex(3);
    m_sleepBtn = new QPushButton("Apply");
    power->addRow("Sleep after", m_sleep);
    power->addRow(QString(), m_sleepBtn);
    root->addWidget(powerBox);

    auto *screenBox = new QGroupBox("Mini screen / clock");
    auto *scr = new QVBoxLayout(screenBox);
    scr->setSpacing(10);
    m_clockBtn = new QPushButton("Sync keyboard clock");
    m_clockBtn->setObjectName("accent");
    m_batt = new QLabel("Battery: —");
    scr->addWidget(m_clockBtn);
    scr->addWidget(m_batt);
    root->addWidget(screenBox);

    m_status = new QLabel;
    root->addWidget(m_status);
    root->addStretch(1);

    connect(m_sleepBtn, &QPushButton::clicked, this, &SettingsWidget::applySleep);
    connect(m_clockBtn, &QPushButton::clicked, this, &SettingsWidget::syncClockNow);
    connect(&m_battTimer, &QTimer::timeout, this, &SettingsWidget::pollBattery);
    m_battTimer.start(15000);
    refreshConnection();
    pollBattery();
}

void SettingsWidget::refreshConnection() {
    Device *d = Device::instance();
    if (d->isConnected())
        m_conn->setText(QString("Connected: %1").arg(d->devicePath()));
    else if (d->connectKeyboard())
        m_conn->setText(QString("Connected: %1").arg(d->devicePath()));
    else
        m_conn->setText(QString("Not connected — %1").arg(d->lastError()));
}

void SettingsWidget::applySleep() {
    int code = m_sleep->currentData().toInt();
    if (Device::instance()->setSleep(code))
        m_status->setText("Sleep timer applied.");
    else
        m_status->setText(QString("Error: %1").arg(Device::instance()->lastError()));
    m_status->setStyleSheet(m_status->text().startsWith("Sleep")
                                ? "color:#7ec97e;" : "color:#e07070;");
}

void SettingsWidget::syncClockNow() {
    if (Device::instance()->syncClock())
        m_status->setText("Clock synchronized.");
    else
        m_status->setText(QString("Error: %1").arg(Device::instance()->lastError()));
    m_status->setStyleSheet(m_status->text().startsWith("Clock")
                                ? "color:#7ec97e;" : "color:#e07070;");
}

void SettingsWidget::pollBattery() {
    Device *d = Device::instance();
    BatteryStatus st = d->battery();
    if (st.valid)
        m_batt->setText(st.wiredFull
                            ? "Battery: wired (full)"
                            : QString("Battery: %1 %").arg(st.percent));
    else
        m_batt->setText("Battery: —");
    refreshConnection();
}

} /* namespace ak820 */
