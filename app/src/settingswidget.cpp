#include "settingswidget.h"

#include "appstate.h"
#include "hidworker.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>
#include <memory>

namespace ak820 {

/* Lighting mode names, mirroring the Effect combo of LightingWidget. */
static QString lightingModeName(uint8_t mode) {
    switch (mode) {
    case ModeStatic: return SettingsWidget::tr("Static");
    case ModeSingleOn: return SettingsWidget::tr("Single On");
    case ModeSingleOff: return SettingsWidget::tr("Single Off");
    case ModeGlittering: return SettingsWidget::tr("Glittering");
    case ModeFalling: return SettingsWidget::tr("Falling");
    case ModeColourful: return SettingsWidget::tr("Colourful");
    case ModeBreath: return SettingsWidget::tr("Breath");
    case ModeSpectrum: return SettingsWidget::tr("Spectrum");
    case ModeOutward: return SettingsWidget::tr("Outward");
    case ModeScrolling: return SettingsWidget::tr("Scrolling");
    case ModeRolling: return SettingsWidget::tr("Rolling");
    case ModeRotating: return SettingsWidget::tr("Rotating");
    case ModeExplode: return SettingsWidget::tr("Explode");
    case ModeLaunch: return SettingsWidget::tr("Launch");
    case ModeRipples: return SettingsWidget::tr("Ripples");
    case ModeFlowing: return SettingsWidget::tr("Flowing");
    case ModePulsating: return SettingsWidget::tr("Pulsating");
    case ModeTilt: return SettingsWidget::tr("Tilt");
    case ModeShuttle: return SettingsWidget::tr("Shuttle");
    case ModeOff: return SettingsWidget::tr("Off (LED Off)");
    case ModeCustom: return SettingsWidget::tr("Custom (per-key)");
    default: return SettingsWidget::tr("Mode 0x%1").arg(mode, 2, 16, QLatin1Char('0')).toUpper();
    }
}

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

    auto *profilesBox = new QGroupBox(tr("Profiles"));
    auto *profLay = new QVBoxLayout(profilesBox);
    profLay->setSpacing(8);
    m_profileList = new QListWidget;
    m_profileList->setMinimumHeight(90);
    profLay->addWidget(m_profileList);

    m_profileSummary = new QLabel;
    m_profileSummary->setObjectName("hint");
    m_profileSummary->setWordWrap(true);
    profLay->addWidget(m_profileSummary);

    auto *profRow1 = new QHBoxLayout;
    auto *saveAsBtn = new QPushButton(tr("Save current as…"));
    auto *importBtn = new QPushButton(tr("Import…"));
    profRow1->addWidget(saveAsBtn);
    profRow1->addWidget(importBtn);
    profRow1->addStretch(1);
    profLay->addLayout(profRow1);

    auto *profRow2 = new QHBoxLayout;
    m_profileApplyBtn = new QPushButton(tr("Apply"));
    m_profileRenameBtn = new QPushButton(tr("Rename"));
    m_profileDeleteBtn = new QPushButton(tr("Delete"));
    m_profileExportBtn = new QPushButton(tr("Export…"));
    profRow2->addWidget(m_profileApplyBtn);
    profRow2->addWidget(m_profileRenameBtn);
    profRow2->addWidget(m_profileDeleteBtn);
    profRow2->addWidget(m_profileExportBtn);
    profRow2->addStretch(1);
    profLay->addLayout(profRow2);
    root->addWidget(profilesBox);

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
    connect(saveAsBtn, &QPushButton::clicked, this, &SettingsWidget::saveProfileAs);
    connect(importBtn, &QPushButton::clicked, this, &SettingsWidget::importProfileFromFile);
    connect(m_profileApplyBtn, &QPushButton::clicked, this, &SettingsWidget::applySelectedProfile);
    connect(m_profileRenameBtn, &QPushButton::clicked, this, &SettingsWidget::renameSelectedProfile);
    connect(m_profileDeleteBtn, &QPushButton::clicked, this, &SettingsWidget::deleteSelectedProfile);
    connect(m_profileExportBtn, &QPushButton::clicked, this, &SettingsWidget::exportSelectedProfile);
    connect(m_profileList, &QListWidget::currentRowChanged,
            this, [this](int) { updateProfileSummary(); updateProfileButtons(); });
    connect(AppState::instance(), &AppState::profilesChanged,
            this, [this]() { refreshProfileList(); });

    m_battTimer.start(15000);
    refreshProfileList();
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

void SettingsWidget::setBusy(bool busy, const QString &message) {
    m_pendingJobs += busy ? 1 : -1;
    const bool idle = m_pendingJobs <= 0;
    m_sleepBtn->setEnabled(idle);
    m_clockBtn->setEnabled(idle);
    if (!message.isEmpty()) {
        m_status->setText(message);
        m_status->setStyleSheet(QString());
    }
}

void SettingsWidget::reportResult(bool ok, const QString &okText, const QString &error) {
    setBusy(false);
    m_status->setText(ok ? okText : tr("Error: %1").arg(error));
    m_status->setStyleSheet(ok ? "color:#7ec97e;" : "color:#e07070;");
}

void SettingsWidget::applySleep() {
    int code = m_sleep->currentData().toInt();
    setBusy(true, tr("Applying sleep timer…"));
    HidWorker::run(this, [code](Device *d) { return d->setSleep(code); },
                   [this](bool ok, QString error) {
                       reportResult(ok, tr("Sleep timer applied."), error);
                   });
}

void SettingsWidget::syncClockNow() {
    setBusy(true, tr("Syncing keyboard clock…"));
    HidWorker::run(this, [](Device *d) { return d->syncClock(); },
                   [this](bool ok, QString error) {
                       reportResult(ok, tr("Clock synchronized."), error);
                   });
}

void SettingsWidget::pollBattery() {
    /* battery() returns a struct, so ferry it out through a shared holder. */
    auto holder = std::make_shared<BatteryStatus>();
    HidWorker::run(this, [holder](Device *d) { *holder = d->battery(); return true; },
                   [this, holder](bool, QString) {
                       if (holder->valid)
                           m_batt->setText(holder->wiredFull
                                               ? tr("Battery: wired (full)")
                                               : tr("Battery: %1 %").arg(holder->percent));
                       else
                           m_batt->setText(tr("Battery: —"));
                       refreshConnection();
                   });
}

void SettingsWidget::refreshProfileList(int selectRow) {
    const auto *st = AppState::instance();
    if (selectRow < 0) selectRow = m_profileList->currentRow();
    m_profileList->clear();
    for (const Profile &p : st->profiles)
        m_profileList->addItem(p.name);
    if (!st->profiles.isEmpty())
        m_profileList->setCurrentRow(qBound(0, selectRow, st->profiles.size() - 1));
    updateProfileSummary();
    updateProfileButtons();
}

void SettingsWidget::updateProfileButtons() {
    const bool hasSelection = m_profileList->currentRow() >= 0;
    m_profileApplyBtn->setEnabled(hasSelection);
    m_profileRenameBtn->setEnabled(hasSelection);
    m_profileDeleteBtn->setEnabled(hasSelection);
    m_profileExportBtn->setEnabled(hasSelection);
}

void SettingsWidget::updateProfileSummary() {
    const auto *st = AppState::instance();
    int row = m_profileList->currentRow();
    if (row < 0 || row >= st->profiles.size()) {
        m_profileSummary->setText(tr("No profile selected."));
        return;
    }
    const Profile &p = st->profiles[row];
    m_profileSummary->setText(tr("%1\n%2 macros — lighting: %3")
                                  .arg(p.name)
                                  .arg(p.macros.size())
                                  .arg(lightingModeName(p.lighting.mode)));
}

void SettingsWidget::saveProfileAs() {
    const auto *st = AppState::instance();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Save profile"),
                                         tr("Profile name:"), QLineEdit::Normal,
                                         tr("My config %1").arg(st->profiles.size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    int index = AppState::instance()->saveProfile(name.trimmed());
    if (index >= 0) {
        refreshProfileList(index);
        m_status->setText(tr("Profile saved."));
        m_status->setStyleSheet("color:#7ec97e;");
    }
}

void SettingsWidget::applySelectedProfile() {
    int row = m_profileList->currentRow();
    const auto *st = AppState::instance();
    if (row < 0 || row >= st->profiles.size()) return;
    auto answer = QMessageBox::question(
        this, tr("Apply profile"),
        tr("Apply profile \"%1\"? This overwrites the current keymap, macros, "
           "custom RGB and lighting.")
            .arg(st->profiles[row].name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (AppState::instance()->applyProfile(row)) {
        m_status->setText(tr("Profile applied."));
        m_status->setStyleSheet("color:#7ec97e;");
    } else {
        m_status->setText(tr("Error: could not apply profile."));
        m_status->setStyleSheet("color:#e07070;");
    }
}

void SettingsWidget::renameSelectedProfile() {
    int row = m_profileList->currentRow();
    auto *st = AppState::instance();
    if (row < 0 || row >= st->profiles.size()) return;
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Rename profile"),
                                         tr("Profile name:"), QLineEdit::Normal,
                                         st->profiles[row].name, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    st->profiles[row].name = name.trimmed();
    st->saveState();
    emit st->profilesChanged();
    refreshProfileList(row);
}

void SettingsWidget::deleteSelectedProfile() {
    int row = m_profileList->currentRow();
    const auto *st = AppState::instance();
    if (row < 0 || row >= st->profiles.size()) return;
    auto answer = QMessageBox::question(this, tr("Delete profile"),
                                        tr("Delete profile \"%1\"?")
                                            .arg(st->profiles[row].name),
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (AppState::instance()->deleteProfile(row)) {
        m_status->setText(tr("Profile deleted."));
        m_status->setStyleSheet("color:#7ec97e;");
    }
}

void SettingsWidget::exportSelectedProfile() {
    int row = m_profileList->currentRow();
    const auto *st = AppState::instance();
    if (row < 0 || row >= st->profiles.size()) return;
    QString path = QFileDialog::getSaveFileName(this, tr("Export profile"),
                                                st->profiles[row].name + QStringLiteral(".json"),
                                                tr("JSON files (*.json)"));
    if (path.isEmpty()) return;
    if (st->exportProfile(row, path)) {
        m_status->setText(tr("Profile exported."));
        m_status->setStyleSheet("color:#7ec97e;");
    } else {
        m_status->setText(tr("Error: could not write the profile file."));
        m_status->setStyleSheet("color:#e07070;");
    }
}

void SettingsWidget::importProfileFromFile() {
    QString path = QFileDialog::getOpenFileName(this, tr("Import profile"), QString(),
                                                tr("JSON files (*.json)"));
    if (path.isEmpty()) return;
    int index = AppState::instance()->importProfile(path);
    if (index >= 0) {
        refreshProfileList(index);
        m_status->setText(tr("Profile imported."));
        m_status->setStyleSheet("color:#7ec97e;");
    } else {
        m_status->setText(tr("Import failed: not a valid ak820-center profile file."));
        m_status->setStyleSheet("color:#e07070;");
    }
}

} /* namespace ak820 */
