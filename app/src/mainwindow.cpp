#include "mainwindow.h"
#include "device.h"
#include "lightingwidget.h"
#include "keymapwidget.h"
#include "rgbwidget.h"
#include "macrowidget.h"
#include "screenwidget.h"
#include "settingswidget.h"
#include "layout.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace ak820 {

MainWindow::MainWindow() {
    setWindowTitle("AK820 Center — AJAZZ AK820 Pro");
    resize(1180, 800);

    Layout::instance(); /* load the official layout once */

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    /* header : titre + badge de connexion */
    auto *header = new QHBoxLayout;
    auto *title = new QLabel("AK820 Center");
    title->setObjectName("headerTitle");
    m_connBadge = new QLabel("Connecting…");
    m_connBadge->setObjectName("hint");
    auto *refreshBtn = new QPushButton("Refresh");
    refreshBtn->setMaximumWidth(90);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_connBadge);
    header->addWidget(refreshBtn);
    root->addLayout(header);

    auto *tabs = new QTabWidget;
    tabs->addTab(new LightingWidget, "Lighting");
    tabs->addTab(new KeymapWidget, "Keys");
    tabs->addTab(new RgbWidget, "Custom RGB");
    tabs->addTab(new MacroWidget, "Macros");
    tabs->addTab(new ScreenWidget, "TFT Screen");
    tabs->addTab(new SettingsWidget, "Settings");
    root->addWidget(tabs, 1);
    setCentralWidget(central);

    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshConnection);
    connect(Device::instance(), &Device::connectedChanged, this,
            [this](bool) { refreshConnection(); });
    refreshConnection();
}

void MainWindow::refreshConnection() {
    Device *d = Device::instance();
    if (d->isConnected()) {
        m_connBadge->setText("● Connected");
        m_connBadge->setStyleSheet("color:#7ec97e; font-weight:700;");
    } else {
        m_connBadge->setText("● Not connected — plug the USB-C cable (wired mode)");
        m_connBadge->setStyleSheet("color:#e0a050; font-weight:700;");
        d->connectKeyboard();
        if (d->isConnected()) {
            m_connBadge->setText("● Connected");
            m_connBadge->setStyleSheet("color:#7ec97e; font-weight:700;");
        }
    }
}

} /* namespace ak820 */
