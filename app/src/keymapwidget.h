/*
 * keymapwidget.h — key remapping, base and Fn layers.
 */
#pragma once

#include "appstate.h"
#include "keyboardview.h"
#include <QWidget>
#include <QDialog>
#include <QPushButton>
#include <QLabel>

namespace ak820 {

class KeymapWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeymapWidget(QWidget *parent = nullptr);

private slots:
    void keyClicked(int lightIndex);
    void applyLayer();
    void applyKeyOnly();
    void resetLayer();
    void switchLayer(int layer);

private:
    void refreshActions();
    void setBusy(bool busy);

    KeyboardView *m_view = nullptr;
    QPushButton *m_layerBase = nullptr;
    QPushButton *m_layerFn = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_applyKey = nullptr;
    QPushButton *m_reset = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    int m_layer = 0;
    int m_lastSlot = -1;   /* slot edited last, target of "apply key only" */
};

/* Single-key assignment dialog. */
class ActionDialog : public QDialog {
    Q_OBJECT
public:
    ActionDialog(const KeyAction &current, int macroCount, QWidget *parent = nullptr);
    KeyAction result() const { return m_result; }

private:
    KeyAction m_result;
};

} /* namespace ak820 */
