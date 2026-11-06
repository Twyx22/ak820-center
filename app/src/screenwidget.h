/*
 * screenwidget.h — 128×128 TFT screen: GIF/image import, text overlay layers
 * with undo, live animated preview, async upload (cmd 0x72 + 4096-byte chunks
 * on the vendor interface, serialized on the HidWorker HID thread).
 */
#pragma once

#include "device.h"

#include <QColor>
#include <QImage>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressDialog;
class QPushButton;
class QSpinBox;

namespace ak820 {

class FramePreview; /* animated 128×128 preview, defined in the .cpp */

/* One text overlay drawn on every frame. */
struct TextLayer {
    QString text;
    int size = 12;          /* font point size */
    QColor color = Qt::white;
    int x = 4, y = 4;       /* top-left position in 128×128 pixels */
};

class ScreenWidget : public QWidget {
    Q_OBJECT
public:
    explicit ScreenWidget(QWidget *parent = nullptr);

private slots:
    void importFiles();
    void upload();
    void addLayer();
    void applyLayer();
    void deleteLayer();
    void undoLayers();
    void layerSelectionChanged();
    void frameCapToggled();
    void uploadProgress(int done, int total); /* invoked queued from the HID thread */

private:
    int maxFrames() const;      /* 30 by default, 140 with the experimental box */
    void renderFrames();        /* layers → frames, RGB565 dedup, cap, preview */
    void pushUndo();
    void refreshLayerList(int selectRow = -1);
    void loadLayerIntoEditor(int row);
    TextLayer layerFromEditor() const;
    void updateColorButton();
    void setBusy(bool busy);

    /* RGB888 → RGB565 LE bytes (one frame's worth). */
    QByteArray toRgb565(const QImage &img) const;
    /* 256-byte header + RGB565 frame data padded to 4096-byte blocks. */
    QVector<uint8_t> encodeAnimation(const QVector<QPair<QImage, int>> &frames,
                                     int *blockCount, QString *err) const;
    /* Whole TFT transfer on the HID thread: open 0xFF68/0xFF67 interface,
       control session, chunks with acks, commit (or abort on failure). */
    static bool uploadTft(const QVector<uint8_t> &payload, int slot, Device *dev,
                          QString *err, const std::function<void(int, int)> &progress);

    QPushButton *m_import = nullptr;
    QPushButton *m_upload = nullptr;
    QSpinBox *m_slot = nullptr;
    QCheckBox *m_allow140 = nullptr;

    QListWidget *m_layerList = nullptr;
    QLineEdit *m_layerText = nullptr;
    QSpinBox *m_layerSize = nullptr;
    QSpinBox *m_layerX = nullptr;
    QSpinBox *m_layerY = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QColor m_layerColor = Qt::white;
    QPushButton *m_addLayer = nullptr;
    QPushButton *m_editLayer = nullptr;
    QPushButton *m_delLayer = nullptr;
    QPushButton *m_undoLayer = nullptr;

    FramePreview *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    QProgressDialog *m_progress = nullptr;

    QVector<QPair<QImage, int>> m_frames;    /* imported source frames + delay ms */
    QVector<QPair<QImage, int>> m_rendered;  /* after layers + dedup + frame cap */
    QVector<TextLayer> m_layers;
    QVector<QVector<TextLayer>> m_undo;      /* snapshot stack for layer ops */
};

} /* namespace ak820 */
