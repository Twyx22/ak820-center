/*
 * mainwindow.h
 */
#pragma once
#include <QMainWindow>

class QLabel;

namespace ak820 {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void refreshConnection();

private:
    QLabel *m_connBadge = nullptr;
};

} /* namespace ak820 */
