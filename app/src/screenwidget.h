/*
 * screenwidget.h — 128×128 TFT screen: GIF/image import, preview,
 * upload (cmd 0x72 + 4096-byte chunks on the vendor interface).
 */
#pragma once

#include "device.h"
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QVector>
#include <QImage>

namespace ak820 {

class ScreenWidget : public QWidget {
    Q_OBJECT
public:
    explicit ScreenWidget(QWidget *parent = nullptr);

private slots:
    void importFiles();
    void preview();
    void upload();

private:
    /* RGB888 → RGB565 LE conversion and legacy payload encoding. */
    QVector<uint8_t> encodeAnimation(const QVector<QPair<QImage, int>> &frames,
                                     int *blockCount, QString *err) const;
    bool uploadTft(const QVector<uint8_t> &payload, QString *err);

    QPushButton *m_import = nullptr, *m_upload = nullptr, *m_previewBtn = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    QSpinBox *m_slot = nullptr;
    QVector<QPair<QImage, int>> m_frames; /* image + delay ms */
};

} /* namespace ak820 */
