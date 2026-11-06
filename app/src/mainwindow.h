/*
 * mainwindow.h — main shell: sidebar navigation, header with connection
 * controls, Home onboarding page, system tray integration.
 */
#pragma once

#include "device.h"

#include <QMainWindow>

class QAction;
class QLabel;
class QListWidget;
class QMenu;
class QStackedWidget;
class QSystemTrayIcon;

namespace ak820 {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *ev) override;

private slots:
    void refreshConnection();
    void updateConnectionBadge();

private:
    QWidget *buildHomePage();
    void refreshDeviceInfo();
    void requestBattery();
    void setupTray();
    void applyTrayPreset(int preset);
    void toggleWindow();

    QListWidget *m_sidebar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_connBadge = nullptr;

    /* Home page (index 0 of the stack). */
    QWidget *m_homeOffline = nullptr;   /* onboarding guidance */
    QWidget *m_homeOnline = nullptr;    /* device info */
    QLabel *m_infoPath = nullptr;
    QLabel *m_infoKind = nullptr;
    QLabel *m_infoBattery = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_showHideAction = nullptr;

    BatteryStatus m_battery;
    bool m_trayNoticeShown = false;
};

} /* namespace ak820 */
