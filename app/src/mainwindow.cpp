#include "mainwindow.h"

#include "appstate.h"
#include "device.h"
#include "hidworker.h"
#include "keymapwidget.h"
#include "layout.h"
#include "lightingwidget.h"
#include "macrowidget.h"
#include "musicwidget.h"
#include "rgbwidget.h"
#include "screenwidget.h"
#include "settingswidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace ak820 {

static QString connectionKindText(ConnectionKind k) {
    switch (k) {
    case ConnectionKind::Wired: return MainWindow::tr("Wired");
    case ConnectionKind::Dongle: return MainWindow::tr("2.4G dongle");
    default: return MainWindow::tr("Not connected");
    }
}

MainWindow::MainWindow() {
    setWindowTitle(tr("AK820 Center — AJAZZ AK820 Pro"));
    setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
    resize(1280, 820);

    Layout::instance(); /* load the official layout once */

    auto *central = new QWidget;
    auto *shell = new QHBoxLayout(central);
    shell->setContentsMargins(10, 10, 16, 10);
    shell->setSpacing(10);

    /* --- sidebar --- */
    m_sidebar = new QListWidget;
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->setViewMode(QListView::IconMode);
    m_sidebar->setResizeMode(QListView::Adjust);
    m_sidebar->setMovement(QListView::Static);
    m_sidebar->setWrapping(false);
    m_sidebar->setIconSize(QSize(144, 36));
    m_sidebar->setGridSize(QSize(152, 62));
    m_sidebar->setSpacing(2);
    m_sidebar->setUniformItemSizes(true);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setFixedWidth(164);

    struct Entry { const char *icon; const char *label; };
    const Entry entries[] = {
        { ":/icons/tab_home.png", QT_TR_NOOP("Home") },
        { ":/icons/tab_light.png", QT_TR_NOOP("Lighting") },
        { ":/icons/tab_customkey.png", QT_TR_NOOP("Keys") },
        { ":/icons/tab_customlight.png", QT_TR_NOOP("Custom RGB") },
        { ":/icons/tab_macro.png", QT_TR_NOOP("Macros") },
        { ":/icons/tab_tft.png", QT_TR_NOOP("TFT Screen") },
        { ":/icons/tab_music.png", QT_TR_NOOP("Music") },
        { ":/icons/tab_config.png", QT_TR_NOOP("Settings") },
    };
    for (const Entry &e : entries) {
        auto *item = new QListWidgetItem(QIcon(QString::fromUtf8(e.icon)), tr(e.label));
        item->setTextAlignment(Qt::AlignHCenter);
        item->setSizeHint(QSize(150, 60));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_sidebar->addItem(item);
    }
    shell->addWidget(m_sidebar);

    /* --- right column: header + pages --- */
    auto *right = new QWidget;
    auto *root = new QVBoxLayout(right);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto *titleBox = new QVBoxLayout;
    auto *title = new QLabel(tr("AK820 Center"));
    title->setObjectName("headerTitle");
    auto *subtitle = new QLabel(tr("AJAZZ AK820 Pro configuration tool"));
    subtitle->setObjectName("hint");
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    titleBox->setSpacing(0);

    m_connBadge = new QLabel;
    m_connBadge->setObjectName("hint");
    auto *wiredBtn = new QPushButton(tr("Wired"));
    auto *dongleBtn = new QPushButton(tr("2.4G dongle"));
    wiredBtn->setMaximumWidth(90);
    dongleBtn->setMaximumWidth(110);
    auto *refreshBtn = new QPushButton(tr("Refresh"));
    refreshBtn->setMaximumWidth(90);

    auto *header = new QHBoxLayout;
    header->addLayout(titleBox);
    header->addStretch(1);
    header->addWidget(m_connBadge);
    header->addWidget(wiredBtn);
    header->addWidget(dongleBtn);
    header->addWidget(refreshBtn);
    root->addLayout(header);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildHomePage()); /* Home (index 0) */
    m_stack->addWidget(new LightingWidget);
    m_stack->addWidget(new KeymapWidget);
    m_stack->addWidget(new RgbWidget);
    m_stack->addWidget(new MacroWidget);
    m_stack->addWidget(new ScreenWidget);
    m_stack->addWidget(new MusicWidget);
    m_stack->addWidget(new SettingsWidget);
    root->addWidget(m_stack, 1);
    shell->addWidget(right, 1);
    setCentralWidget(central);

    connect(m_sidebar, &QListWidget::currentRowChanged,
            m_stack, &QStackedWidget::setCurrentIndex);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshConnection);
    connect(wiredBtn, &QPushButton::clicked, this, [this] {
        /* Connection attempt on the HID thread; the badge is refreshed
           through connectedChanged. */
        HidWorker::run(this, [](Device *dev) { return dev->connectKeyboard(); }, nullptr);
    });
    connect(dongleBtn, &QPushButton::clicked, this, [this] {
        HidWorker::run(this, [](Device *dev) { return dev->connectDongle(); }, nullptr);
    });
    connect(Device::instance(), &Device::connectedChanged, this,
            [this](bool) { updateConnectionBadge(); });

    setupTray();
    m_sidebar->setCurrentRow(0);
    updateConnectionBadge();
}

QWidget *MainWindow::buildHomePage() {
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    /* Onboarding guidance, shown while no keyboard is connected. */
    m_homeOffline = new QWidget;
    auto *offLay = new QVBoxLayout(m_homeOffline);
    offLay->setContentsMargins(0, 0, 0, 0);
    offLay->setSpacing(10);
    auto *guide = new QLabel(tr(
        "<h3>No keyboard detected</h3>"
        "<p><b>Wired:</b> plug the USB-C cable, then press the \"Wired\" button in the header.</p>"
        "<p><b>Wireless:</b> plug the 2.4G dongle, switch the keyboard to 2.4G mode, "
        "then press the \"2.4G dongle\" button.</p>"
        "<p><b>Linux access rights:</b> install the udev rule "
        "(<code>packaging/99-ajazz-ak820pro.rules</code> into <code>/etc/udev/rules.d/</code>) "
        "and add your user to the <code>input</code> group, then log out and back in.</p>"));
    guide->setObjectName("hint");
    guide->setTextFormat(Qt::RichText);
    guide->setWordWrap(true);
    guide->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    offLay->addWidget(guide);
    offLay->addStretch(1);
    root->addWidget(m_homeOffline);

    /* Device info, shown once a keyboard is connected. */
    m_homeOnline = new QWidget;
    auto *form = new QFormLayout(m_homeOnline);
    form->setSpacing(10);
    m_infoPath = new QLabel;
    m_infoPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_infoKind = new QLabel;
    m_infoBattery = new QLabel(tr("Battery: unknown — press \"Read battery\""));
    auto *battBtn = new QPushButton(tr("Read battery"));
    form->addRow(tr("Device path"), m_infoPath);
    form->addRow(tr("Connection"), m_infoKind);
    form->addRow(tr("Battery"), m_infoBattery);
    form->addRow(battBtn);
    connect(battBtn, &QPushButton::clicked, this, &MainWindow::requestBattery);
    root->addWidget(m_homeOnline);
    root->addStretch(1);
    return page;
}

void MainWindow::updateConnectionBadge() {
    Device *d = Device::instance();
    switch (d->connectionKind()) {
    case ConnectionKind::Wired:
        m_connBadge->setText(tr("● Wired"));
        m_connBadge->setStyleSheet(QStringLiteral("color:#7ec97e; font-weight:700;"));
        break;
    case ConnectionKind::Dongle:
        m_connBadge->setText(tr("● 2.4G dongle"));
        m_connBadge->setStyleSheet(QStringLiteral("color:#7ec97e; font-weight:700;"));
        break;
    default:
        m_connBadge->setText(tr("● Not connected"));
        m_connBadge->setStyleSheet(QStringLiteral("color:#e0a050; font-weight:700;"));
        break;
    }
    const bool connected = d->isConnected();
    if (m_homeOffline) m_homeOffline->setVisible(!connected);
    if (m_homeOnline) m_homeOnline->setVisible(connected);
    if (connected)
        refreshDeviceInfo();
    else if (m_infoBattery)
        m_infoBattery->setText(tr("Battery: unknown — press \"Read battery\""));
}

void MainWindow::refreshDeviceInfo() {
    Device *d = Device::instance();
    m_infoPath->setText(d->devicePath().isEmpty() ? tr("(unavailable)") : d->devicePath());
    m_infoKind->setText(connectionKindText(d->connectionKind()));
}

void MainWindow::requestBattery() {
    m_infoBattery->setText(tr("Battery: reading…"));
    HidWorker::run(this, [this](Device *dev) {
        m_battery = dev->battery();
        return m_battery.valid;
    }, [this](bool ok, QString) {
        if (!ok) {
            m_infoBattery->setText(tr("Battery: unavailable"));
            return;
        }
        if (m_battery.wiredFull)
            m_infoBattery->setText(tr("Battery: full (wired power)"));
        else
            m_infoBattery->setText(tr("Battery: %1%").arg(m_battery.percent));
    });
}

void MainWindow::refreshConnection() {
    /* Re-probe for a wired keyboard without blocking the UI. */
    HidWorker::run(this, [](Device *dev) {
        return dev->isConnected() || dev->connectKeyboard();
    }, [this](bool, QString) { updateConnectionBadge(); });
}

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icon.png")), this);
    auto *menu = new QMenu(this);

    m_showHideAction = menu->addAction(tr("Hide window"), this, [this] { toggleWindow(); });

    QMenu *presets = menu->addMenu(tr("Lighting presets"));
    presets->addAction(tr("Off"), this, [this] { applyTrayPreset(0); });
    presets->addAction(tr("Static white"), this, [this] { applyTrayPreset(1); });
    presets->addAction(tr("Spectrum"), this, [this] { applyTrayPreset(2); });
    presets->addAction(tr("Breath"), this, [this] { applyTrayPreset(3); });

    menu->addSeparator();
    menu->addAction(tr("Quit"), qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    m_tray->setToolTip(tr("AK820 Center"));
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            toggleWindow();
    });
    m_tray->show();
}

void MainWindow::applyTrayPreset(int preset) {
    LightingConfig cfg;
    switch (preset) {
    case 0: cfg.mode = ModeOff; break;
    case 1: cfg.mode = ModeStatic; cfg.r = cfg.g = cfg.b = 255; break;
    case 2: cfg.mode = ModeSpectrum; break;
    default: cfg.mode = ModeBreath; cfg.r = cfg.g = cfg.b = 255; break;
    }
    /* Persist so the Lighting page reflects the applied effect. */
    AppState::instance()->setLightingConfig(cfg);
    HidWorker::run(nullptr, [cfg](Device *dev) { return dev->setLighting(cfg); }, nullptr);
}

void MainWindow::toggleWindow() {
    if (isVisible() && !isMinimized()) {
        hide();
        if (m_showHideAction) m_showHideAction->setText(tr("Show window"));
    } else {
        showNormal();
        raise();
        activateWindow();
        if (m_showHideAction) m_showHideAction->setText(tr("Hide window"));
    }
}

void MainWindow::closeEvent(QCloseEvent *ev) {
    if (m_tray && m_tray->isVisible()) {
        ev->ignore();
        hide();
        if (m_showHideAction) m_showHideAction->setText(tr("Show window"));
        if (!m_trayNoticeShown) {
            m_trayNoticeShown = true;
            m_tray->showMessage(windowTitle(),
                                tr("AK820 Center is still running in the system tray."),
                                QSystemTrayIcon::Information, 2500);
        }
    } else {
        QMainWindow::closeEvent(ev);
    }
}

} /* namespace ak820 */
