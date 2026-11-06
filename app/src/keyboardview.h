/*
 * keyboardview.h — keyboard rendering from the official layout.
 * Left click: select; right click: context action.
 * Remap mode: shows the assigned action; RGB mode: the color.
 */
#pragma once

#include "layout.h"
#include <QWidget>
#include <QColor>
#include <QHash>
#include <QSet>
#include <functional>

namespace ak820 {

class KeyboardView : public QWidget {
    Q_OBJECT
public:
    enum Mode { ModePlain, ModeRemap, ModeRgb };

    explicit KeyboardView(QWidget *parent = nullptr);

    void setLayout(const Layout *layout);
    void setMode(Mode m) { m_mode = m; update(); }
    Mode mode() const { return m_mode; }

    void setActionForSlot(int lightIndex, const QString &label);
    void setColorForSlot(int lightIndex, const QColor &c);
    void setSlotSelected(int lightIndex, bool sel);
    void clearActions();
    void clearColors();
    int selectedSlot() const { return m_selected; }
    void setSelectedSlot(int li) { m_selected = li; update(); }

signals:
    void slotActivated(int lightIndex);          /* clic gauche */
    void slotContextMenu(int lightIndex, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void leaveEvent(QEvent *ev) override;
    QSize sizeHint() const override;

private:
    const Layout *m_layout = nullptr;
    Mode m_mode = ModePlain;
    QHash<int, QString> m_actions;
    QHash<int, QColor> m_colors;
    QSet<int> m_selectedSet;
    int m_selected = -1;
    int m_hovered = -1;
    int hitTest(const QPoint &pos) const;
};

} /* namespace ak820 */
