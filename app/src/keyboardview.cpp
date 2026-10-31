#include "keyboardview.h"

#include <QPainter>
#include <QMouseEvent>
#include <cmath>

namespace ak820 {

KeyboardView::KeyboardView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(880, 360);
    setMouseTracking(true);
}

void KeyboardView::setLayout(const Layout *layout) {
    m_layout = layout;
    m_actions.clear();
    m_colors.clear();
    m_selected = -1;
    update();
}

void KeyboardView::setActionForSlot(int li, const QString &label) {
    if (label.isEmpty()) m_actions.remove(li);
    else m_actions[li] = label;
    update();
}

void KeyboardView::setColorForSlot(int li, const QColor &c) {
    m_colors[li] = c;
    update();
}

void KeyboardView::setSlotSelected(int li, bool sel) {
    if (sel) m_selectedSet.insert(li);
    else m_selectedSet.remove(li);
    update();
}

void KeyboardView::clearActions() { m_actions.clear(); update(); }
void KeyboardView::clearColors() { m_colors.clear(); update(); }

int KeyboardView::hitTest(const QPoint &pos) const {
    if (!m_layout) return -1;
    /* mise à l'échelle du rect 800x300 du layout dans le widget */
    const int pad = 20;
    double sx = (width() - 2.0 * pad) / m_layout->keyboardW;
    double sy = (height() - 2.0 * pad) / m_layout->keyboardH;
    double s = qMin(sx, sy);
    for (const LayoutKey &k : m_layout->keys()) {
        QRect r(k.rect.left() * s + pad, k.rect.top() * s + pad,
                k.rect.width() * s, k.rect.height() * s);
        if (r.contains(pos)) return k.lightIndex;
    }
    return -1;
}

void KeyboardView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(24, 24, 28));

    if (!m_layout) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, tr("Loading layout…"));
        return;
    }
    const int pad = 20;
    double sx = (width() - 2.0 * pad) / m_layout->keyboardW;
    double sy = (height() - 2.0 * pad) / m_layout->keyboardH;
    double s = qMin(sx, sy);

    QFont f = font();
    f.setPointSizeF(qBound(7.0, 8.5 * s, 13.0));
    p.setFont(f);

    for (const LayoutKey &k : m_layout->keys()) {
        QRect r(k.rect.left() * s + pad, k.rect.top() * s + pad,
                k.rect.width() * s, k.rect.height() * s);
        r.adjust(1, 1, -2, -2);

        QColor keyFill(44, 44, 52);
        if (m_mode == ModeRgb && m_colors.contains(k.lightIndex))
            keyFill = m_colors[k.lightIndex];
        if (m_selected == k.lightIndex) keyFill = keyFill.lighter(160);

        p.setPen(QPen(m_selected == k.lightIndex ? QColor(255, 190, 60)
                                                 : QColor(20, 20, 24),
                      m_selected == k.lightIndex ? 2 : 1));
        p.setBrush(keyFill);
        p.drawRoundedRect(r, 4 * s, 4 * s);

        QString label = k.name;
        if (m_mode == ModeRemap && m_actions.contains(k.lightIndex))
            label = m_actions[k.lightIndex];
        p.setPen(m_mode == ModeRgb && m_colors.contains(k.lightIndex)
                     ? QColor(0, 0, 0) : QColor(220, 220, 220));
        p.drawText(r.adjusted(2, 1, -2, -1), Qt::AlignCenter | Qt::TextWordWrap, label);
    }
}

void KeyboardView::mousePressEvent(QMouseEvent *ev) {
    int li = hitTest(ev->pos());
    if (li < 0) return;
    m_selected = li;
    update();
    if (ev->button() == Qt::LeftButton)
        emit slotActivated(li);
    else if (ev->button() == Qt::RightButton)
        emit slotContextMenu(li, ev->globalPosition().toPoint());
}

void KeyboardView::mouseMoveEvent(QMouseEvent *ev) {
    int li = hitTest(ev->pos());
    setCursor(li >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

} /* namespace ak820 */
