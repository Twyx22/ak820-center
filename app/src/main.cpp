#include "mainwindow.h"
#include "hidworker.h"

#include <QApplication>
#include <QFont>
#include <QLocale>
#include <QTranslator>

static const char *kDarkStyle = R"(
QWidget { background: #1b1c20; color: #e4e4e8; font-size: 13px; }
QMainWindow { background: #17181c; }

QTabWidget::pane { border: 1px solid #2a2c33; border-radius: 6px; top: -1px; }
QTabBar::tab {
    background: #222329; color: #9a9aa4; padding: 8px 18px;
    border: 1px solid #2a2c33; border-bottom: none;
    border-top-left-radius: 6px; border-top-right-radius: 6px;
    margin-right: 3px; font-weight: 600;
}
QTabBar::tab:selected { background: #2e3038; color: #ffffff; }
QTabBar::tab:hover { color: #d8d8de; }

QPushButton {
    background: #2e3038; border: 1px solid #3a3d47; border-radius: 6px;
    padding: 7px 16px; font-weight: 600;
}
QPushButton:hover { background: #383b45; }
QPushButton:pressed { background: #44485455; }
QPushButton:disabled { color: #6a6a72; background: #26272d; }
QPushButton#accent {
    background: #c95f2d; border-color: #d96f3d; color: white;
}
QPushButton#accent:hover { background: #d96f3d; }

QComboBox {
    background: #26272d; border: 1px solid #3a3d47; border-radius: 6px;
    padding: 6px 10px; min-width: 160px;
}
QComboBox:hover { border-color: #4a4e5a; }
QComboBox QAbstractItemView {
    background: #26272d; border: 1px solid #3a3d47;
    selection-background-color: #c95f2d;
}

QSlider::groove:horizontal { height: 6px; background: #2a2c33; border-radius: 3px; }
QSlider::handle:horizontal {
    width: 16px; margin: -6px 0; border-radius: 8px;
    background: #c95f2d;
}
QSlider::sub-page:horizontal { background: #7a4326; border-radius: 3px; }

QGroupBox {
    border: 1px solid #2a2c33; border-radius: 8px; margin-top: 14px;
    padding: 10px 8px 8px 8px; font-weight: 700; color: #cfcfd6;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }

QLabel#statusOk { color: #7ec97e; font-weight: 700; }
QLabel#statusErr { color: #e07070; font-weight: 700; }
QLabel#hint { color: #8a8a94; }
QLabel#headerTitle { font-size: 17px; font-weight: 800; color: #ffffff; }

QListWidget#sidebar { background: #17181c; }
QListWidget#sidebar::item {
    color: #9a9aa4; padding: 4px 2px; border-radius: 6px;
}
QListWidget#sidebar::item:hover { color: #d8d8de; }
QListWidget#sidebar::item:selected { background: #2e3038; color: #ffffff; }

QListWidget, QTableWidget, QLineEdit, QSpinBox {
    background: #222329; border: 1px solid #3a3d47; border-radius: 6px; padding: 4px;
}
QTableWidget::item:selected, QListWidget::item:selected { background: #c95f2d; }
QHeaderView::section {
    background: #26272d; border: none; border-right: 1px solid #2a2c33;
    padding: 6px; font-weight: 700; color: #9a9aa4;
}
)";

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("ak820-center");
    QApplication::setOrganizationName("ak820");

    /* Qt translations (embedded .qm files under ":/i18n" by
       qt_add_translations; fall back to the build directory). */
    QTranslator translator;
    if (translator.load(QLocale::system(), QStringLiteral("ak820-center"),
                        QStringLiteral("_"), QStringLiteral(":/i18n")) ||
        translator.load(QLocale::system(), QStringLiteral("ak820-center"),
                        QStringLiteral("_"), QCoreApplication::applicationDirPath())) {
        QApplication::installTranslator(&translator);
    }

    app.setStyleSheet(QString::fromUtf8(kDarkStyle));
    QApplication::setFont(QFont("Inter", 10));
    /* Blocking HID jobs run on the worker's own thread (see hidworker.h). */
    ak820::HidWorker::startWorker();
    ak820::MainWindow win;
    win.show();
    const int rc = app.exec();
    ak820::HidWorker::stopWorker();
    return rc;
}
