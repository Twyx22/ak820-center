#include "macrowidget.h"
#include "hidworker.h"
#include "keycodes.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QGroupBox>

namespace ak820 {

MacroWidget::MacroWidget(QWidget *parent) : QWidget(parent) {
    m_list = new QListWidget;
    m_actions = new QTableWidget(0, 3);
    m_actions->setHorizontalHeaderLabels({ "Delay (ms)", "Action", "Type" });
    m_actions->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_actions->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_actions->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_new = new QPushButton("New macro");
    m_del = new QPushButton("Delete");
    m_send = new QPushButton("Write macros to keyboard");
    m_send->setObjectName("accent");
    m_add = new QPushButton("Add action");
    m_delAct = new QPushButton("Remove action");
    m_hint = new QLabel("Macros play from keys assigned in the Keys tab (assignment type: Macro).");
    m_hint->setObjectName("hint");
    m_status = new QLabel;

    m_type = new QComboBox;
    m_type->addItem("Key press", 0xB0);
    m_type->addItem("Key release", 0x30);
    m_type->addItem("Mouse click", 0x90);
    m_type->addItem("Mouse release", 0x10);
    m_type->addItem("Pause", 0x50);

    m_key = new QComboBox;
    for (int c = 0x04; c <= 0x73; c++)
        m_key->addItem(hidKeyName(c), c);
    for (int c = 0xE0; c <= 0xE7; c++)
        m_key->addItem(hidKeyName(c), c);

    m_delay = new QSpinBox;
    m_delay->setRange(0, 65535);
    m_delay->setSuffix(" ms");

    auto *leftBox = new QGroupBox("Macros");
    auto *left = new QVBoxLayout(leftBox);
    left->setContentsMargins(6, 6, 6, 6);
    left->addWidget(m_list);
    auto *listBtns = new QHBoxLayout;
    listBtns->addWidget(m_new);
    listBtns->addWidget(m_del);
    left->addLayout(listBtns);

    auto *rightBox = new QGroupBox("Actions");
    auto *right = new QVBoxLayout(rightBox);
    right->setContentsMargins(6, 6, 6, 6);
    right->addWidget(m_actions);
    auto *form = new QFormLayout;
    form->setSpacing(8);
    form->addRow("Type", m_type);
    form->addRow("Key / button", m_key);
    form->addRow("Delay before", m_delay);
    auto *actBtns = new QHBoxLayout;
    actBtns->addWidget(m_add);
    actBtns->addWidget(m_delAct);
    actBtns->addStretch(1);
    form->addRow(QString(), actBtns);
    right->addLayout(form);

    auto *split = new QHBoxLayout;
    split->addWidget(leftBox, 0);
    split->addWidget(rightBox, 1);
    leftBox->setMaximumWidth(280);

    auto *sendRow = new QHBoxLayout;
    sendRow->addStretch(1);
    sendRow->addWidget(m_send);
    sendRow->addStretch(1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    root->addLayout(split, 1);
    root->addWidget(m_hint);
    root->addLayout(sendRow);
    root->addWidget(m_status);

    connect(m_new, &QPushButton::clicked, this, &MacroWidget::newMacro);
    connect(m_del, &QPushButton::clicked, this, &MacroWidget::deleteMacro);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { macroSelected(); });
    connect(m_add, &QPushButton::clicked, this, &MacroWidget::addAction);
    connect(m_delAct, &QPushButton::clicked, this, &MacroWidget::deleteAction);
    connect(m_send, &QPushButton::clicked, this, &MacroWidget::sendMacros);
    refreshList();
}

void MacroWidget::refreshList() {
    m_list->clear();
    for (const Macro &m : AppState::instance()->macros)
        m_list->addItem(m.name);
}

void MacroWidget::refreshActions() {
    m_actions->setRowCount(0);
    int idx = currentIndex();
    if (idx < 0) return;
    const auto &acts = AppState::instance()->macros[idx].actions;
    for (int i = 0; i < acts.size(); i++) {
        m_actions->insertRow(i);
        m_actions->setItem(i, 0, new QTableWidgetItem(QString::number(acts[i].delayMs)));
        if ((acts[i].flags & 0x50) == 0x50)
            m_actions->setItem(i, 1, new QTableWidgetItem("pause"));
        else if ((acts[i].flags & 0x10) == 0x10)
            m_actions->setItem(i, 1, new QTableWidgetItem(QString("mouse 0x%1").arg(acts[i].key, 2, 16, QChar('0'))));
        else
            m_actions->setItem(i, 1, new QTableWidgetItem(hidKeyName(acts[i].key)));
        m_actions->setItem(i, 2, new QTableWidgetItem(
            (acts[i].flags & 0x80) ? "press" : "release"));
    }
}

int MacroWidget::currentIndex() const {
    return m_list->currentRow();
}

void MacroWidget::newMacro() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New macro", "Name:",
                                         QLineEdit::Normal,
                                         QString("Macro %1").arg(AppState::instance()->macros.size() + 1), &ok);
    if (!ok || name.isEmpty()) return;
    Macro m;
    m.name = name;
    AppState::instance()->macros.append(m);
    AppState::instance()->saveState();
    refreshList();
    m_list->setCurrentRow(AppState::instance()->macros.size() - 1);
}

void MacroWidget::deleteMacro() {
    int idx = currentIndex();
    if (idx < 0) return;
    auto &m = AppState::instance()->macros;
    m.removeAt(idx);
    AppState::instance()->saveState();
    refreshList();
    refreshActions();
}

void MacroWidget::macroSelected() { refreshActions(); }

void MacroWidget::addAction() {
    int idx = currentIndex();
    if (idx < 0) {
        m_status->setText("Select a macro first.");
        m_status->setStyleSheet("color:#e0a050;");
        return;
    }
    MacroAction a;
    a.flags = (uint8_t)m_type->currentData().toInt();
    a.key = (uint8_t)m_key->currentData().toInt();
    a.delayMs = (uint16_t)m_delay->value();
    AppState::instance()->macros[idx].actions.append(a);
    AppState::instance()->saveState();
    refreshActions();
}

void MacroWidget::deleteAction() {
    int idx = currentIndex();
    int row = m_actions->currentRow();
    if (idx < 0 || row < 0) return;
    AppState::instance()->macros[idx].actions.removeAt(row);
    AppState::instance()->saveState();
    refreshActions();
}

void MacroWidget::sendMacros() {
    auto *st = AppState::instance();
    bool ok = false;
    QVector<uint8_t> payload = st->encodeMacroPayload(&ok);
    if (!ok) {
        m_status->setText(tr("Macro space exceeded (3072 bytes)."));
        m_status->setStyleSheet("color:#e07070;");
        return;
    }
    m_send->setEnabled(false);
    m_status->setText(tr("Writing macros…"));
    m_status->setStyleSheet(QString());
    const int size = payload.size();
    const int blocks = (qMax(size, 400) + 63) / 64;
    /* Whole 3-step session (open → payload → commit/finish, see device.cpp)
       runs as one job on the HID thread; the GUI only sees the completion. */
    HidWorker::run(this,
        [payload, blocks](Device *dev) {
            if (!dev->openMacroSession()) return false;
            return dev->writeMacroPayload(payload, blocks);
        },
        [this, size](bool ok, QString err) {
            m_send->setEnabled(true);
            if (ok) {
                m_status->setText(tr("Macros written (%1 bytes).").arg(size));
                m_status->setStyleSheet("color:#7ec97e;");
            } else {
                m_status->setText(tr("Error: %1").arg(err));
                m_status->setStyleSheet("color:#e07070;");
            }
        });
}

} /* namespace ak820 */
