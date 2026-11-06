/*
 * settingswidget.h — connection, auto-sleep, clock sync, battery, profiles.
 */
#pragma once

#include "device.h"
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
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
    void saveProfileAs();
    void applySelectedProfile();
    void renameSelectedProfile();
    void deleteSelectedProfile();
    void exportSelectedProfile();
    void importProfileFromFile();
    void updateProfileSummary();

private:
    void setBusy(bool busy, const QString &message = QString());
    void reportResult(bool ok, const QString &okText, const QString &error);
    void refreshProfileList(int selectRow = -1);
    void updateProfileButtons();

    QComboBox *m_sleep = nullptr;
    QPushButton *m_sleepBtn = nullptr;
    QPushButton *m_clockBtn = nullptr;
    QLabel *m_batt = nullptr;
    QLabel *m_conn = nullptr;
    QLabel *m_status = nullptr;
    QTimer m_battTimer;

    QListWidget *m_profileList = nullptr;
    QLabel *m_profileSummary = nullptr;
    QPushButton *m_profileApplyBtn = nullptr;
    QPushButton *m_profileRenameBtn = nullptr;
    QPushButton *m_profileDeleteBtn = nullptr;
    QPushButton *m_profileExportBtn = nullptr;
    int m_pendingJobs = 0;
};

} /* namespace ak820 */
