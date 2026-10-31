/*
 * macrowidget.h — macro manager: list, action editor, firmware upload.
 */
#pragma once

#include "appstate.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>

namespace ak820 {

class MacroWidget : public QWidget {
    Q_OBJECT
public:
    explicit MacroWidget(QWidget *parent = nullptr);

private slots:
    void newMacro();
    void deleteMacro();
    void macroSelected();
    void addAction();
    void deleteAction();
    void sendMacros();

private:
    void refreshList();
    void refreshActions();
    int currentIndex() const;

    QListWidget *m_list = nullptr;
    QTableWidget *m_actions = nullptr;
    QPushButton *m_new = nullptr, *m_del = nullptr;
    QPushButton *m_add = nullptr, *m_delAct = nullptr;
    QPushButton *m_send = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    QComboBox *m_key = nullptr;
    QComboBox *m_type = nullptr;
    QSpinBox *m_delay = nullptr;
};

} /* namespace ak820 */
