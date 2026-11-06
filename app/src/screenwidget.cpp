#include "screenwidget.h"

#include "hidworker.h"

#include <hidapi/hidapi.h>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace ak820 {

static const uint16_t VID = 0x0C45;
static const uint16_t TFT_PIDS[] = { 0x8009, 0x800A };
static constexpr int TFT_W = 128, TFT_H = 128;
static constexpr int FRAME_BYTES = TFT_W * TFT_H * 2;      /* RGB565 */
static constexpr int HEADER_BYTES = 256;                    /* gif_headlength */
static constexpr int CHUNK_BYTES = 4096;
static constexpr int MAX_FRAMES_DEFAULT = 30;               /* verified legacy limit */
static constexpr int MAX_FRAMES_EXPERIMENTAL = 140;         /* officially announced */
static constexpr int MAX_DELAY_MS = 510;                    /* 255 × 2 ms units */

/* Animated 128×128 preview: plays m_frames with per-frame delays. */
class FramePreview : public QWidget {
    Q_OBJECT
public:
    explicit FramePreview(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(TFT_W * 2, TFT_H * 2);
        setStyleSheet("background:#0d0d10; border:1px solid #3a3d47; border-radius:8px;");
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (m_frames.isEmpty()) return;
            m_idx = (m_idx + 1) % m_frames.size();
            m_timer.start(qMax(20, m_frames[m_idx].second));
            update();
        });
    }

    void setFrames(const QVector<QPair<QImage, int>> &frames) {
        m_frames = frames;
        m_idx = 0;
        m_timer.stop();
        if (m_frames.size() > 1)
            m_timer.start(qMax(20, m_frames[0].second));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#0d0d10"));
        if (m_frames.isEmpty()) {
            p.setPen(QColor("#8a8f9e"));
            p.drawText(rect(), Qt::AlignCenter, tr("No frames — import a GIF or image"));
            return;
        }
        p.drawImage(rect(), m_frames[m_idx].first);
    }

private:
    QVector<QPair<QImage, int>> m_frames;
    QTimer m_timer;
    int m_idx = 0;
};

ScreenWidget::ScreenWidget(QWidget *parent) : QWidget(parent) {
    m_import = new QPushButton(tr("Import GIF / images (resized to 128×128)…"));
    m_upload = new QPushButton(tr("Send to screen"));
    m_upload->setObjectName("accent");

    m_allow140 = new QCheckBox(tr("Allow up to 140 frames (experimental)"));
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_preview = new FramePreview;
    m_hint = new QLabel(tr("The animation is stored in the selected slot. "
                           "Press Fn + Delete on the keyboard to switch between the clock and your animation. "
                           "Text layers are drawn on every frame."));
    m_hint->setObjectName("hint");
    m_hint->setWordWrap(true);

    m_progress = new QProgressDialog(this);
    m_progress->setCancelButton(nullptr);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->hide();

    m_slot = new QSpinBox;
    m_slot->setRange(1, 3);
    m_slot->setValue(1);

    auto *slotRow = new QHBoxLayout;
    slotRow->addWidget(new QLabel(tr("Destination slot:")));
    slotRow->addWidget(m_slot);
    slotRow->addWidget(m_allow140);
    slotRow->addStretch(1);

    auto *btns = new QHBoxLayout;
    btns->addWidget(m_import);
    btns->addWidget(m_upload);
    btns->addStretch(1);

    /* Text overlay editor. */
    m_layerList = new QListWidget;
    m_layerList->setMaximumHeight(96);
    m_layerText = new QLineEdit("AK820");
    m_layerSize = new QSpinBox;
    m_layerSize->setRange(6, 48);
    m_layerSize->setValue(12);
    m_layerSize->setSuffix(tr(" pt"));
    m_layerX = new QSpinBox;
    m_layerX->setRange(0, TFT_W - 1);
    m_layerY = new QSpinBox;
    m_layerY->setRange(0, TFT_H - 1);
    m_colorBtn = new QPushButton(tr("Color…"));
    m_addLayer = new QPushButton(tr("Add"));
    m_editLayer = new QPushButton(tr("Apply"));
    m_delLayer = new QPushButton(tr("Delete"));
    m_undoLayer = new QPushButton(tr("Undo"));
    updateColorButton();

    auto *layerForm = new QFormLayout;
    layerForm->setSpacing(6);
    layerForm->addRow(tr("Text"), m_layerText);
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_layerSize);
    sizeRow->addWidget(m_colorBtn);
    sizeRow->addStretch(1);
    layerForm->addRow(tr("Size / color"), sizeRow);
    auto *posRow = new QHBoxLayout;
    posRow->addWidget(new QLabel(tr("X")));
    posRow->addWidget(m_layerX);
    posRow->addWidget(new QLabel(tr("Y")));
    posRow->addWidget(m_layerY);
    posRow->addStretch(1);
    layerForm->addRow(tr("Position"), posRow);
    auto *layerBtns = new QHBoxLayout;
    layerBtns->addWidget(m_addLayer);
    layerBtns->addWidget(m_editLayer);
    layerBtns->addWidget(m_delLayer);
    layerBtns->addWidget(m_undoLayer);
    layerBtns->addStretch(1);
    layerForm->addRow(QString(), layerBtns);

    auto *layersBox = new QGroupBox(tr("Text layers"));
    auto *layersCol = new QVBoxLayout(layersBox);
    layersCol->setContentsMargins(6, 6, 6, 6);
    layersCol->addWidget(m_layerList);
    layersCol->addLayout(layerForm);

    auto *mid = new QHBoxLayout;
    mid->addWidget(m_preview, 0, Qt::AlignTop);
    mid->addWidget(layersBox, 1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    root->addLayout(btns);
    root->addLayout(slotRow);
    root->addLayout(mid);
    root->addWidget(m_status);
    root->addWidget(m_hint);
    root->addStretch(1);

    connect(m_import, &QPushButton::clicked, this, &ScreenWidget::importFiles);
    connect(m_upload, &QPushButton::clicked, this, &ScreenWidget::upload);
    connect(m_allow140, &QCheckBox::toggled, this, &ScreenWidget::frameCapToggled);
    connect(m_layerList, &QListWidget::currentRowChanged,
            this, &ScreenWidget::layerSelectionChanged);
    connect(m_addLayer, &QPushButton::clicked, this, &ScreenWidget::addLayer);
    connect(m_editLayer, &QPushButton::clicked, this, &ScreenWidget::applyLayer);
    connect(m_delLayer, &QPushButton::clicked, this, &ScreenWidget::deleteLayer);
    connect(m_undoLayer, &QPushButton::clicked, this, &ScreenWidget::undoLayers);
    connect(m_colorBtn, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_layerColor, this, tr("Layer color"));
        if (c.isValid()) {
            m_layerColor = c;
            updateColorButton();
        }
    });

    refreshLayerList();
    layerSelectionChanged();
}

int ScreenWidget::maxFrames() const {
    return m_allow140->isChecked() ? MAX_FRAMES_EXPERIMENTAL : MAX_FRAMES_DEFAULT;
}

void ScreenWidget::importFiles() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Images or GIF"), QString(),
        tr("Images (*.gif *.png *.jpg *.jpeg *.bmp)"));
    if (files.isEmpty()) return;
    m_frames.clear();
    for (const QString &f : files) {
        QImageReader rd(f);
        if (rd.imageCount() > 1 || f.endsWith(".gif", Qt::CaseInsensitive)) {
            int i = 0;
            while (i < MAX_FRAMES_EXPERIMENTAL) {
                QImage img;
                if (!rd.read(&img)) break;
                int delay = rd.nextImageDelay();
                m_frames.append({ img.scaled(TFT_W, TFT_H, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation),
                                  qBound(20, delay, 1275) });
                i++;
            }
        } else {
            QImage img;
            if (rd.read(&img))
                m_frames.append({ img.scaled(TFT_W, TFT_H, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation), 100 });
        }
        if (m_frames.size() >= MAX_FRAMES_EXPERIMENTAL) break;
    }
    m_undo.clear(); /* layers survive an import; the undo stack stays layer-scoped */
    m_undoLayer->setEnabled(false);
    renderFrames();
}

QByteArray ScreenWidget::toRgb565(const QImage &img) const {
    QImage src = img.convertToFormat(QImage::Format_RGB888);
    QByteArray out;
    out.resize(FRAME_BYTES);
    char *dst = out.data();
    for (int y = 0; y < TFT_H; y++) {
        const uchar *line = src.constScanLine(y);
        for (int x = 0; x < TFT_W; x++) {
            uint8_t r = line[x * 3], g = line[x * 3 + 1], b = line[x * 3 + 2];
            uint16_t px = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            *dst++ = (char)(px & 0xff);
            *dst++ = (char)(px >> 8);
        }
    }
    return out;
}

void ScreenWidget::renderFrames() {
    /* 1. Draw every text layer onto a copy of each frame. */
    QVector<QPair<QImage, int>> drawn;
    for (const auto &fr : m_frames) {
        QImage img = fr.first.convertToFormat(QImage::Format_RGB32);
        if (!m_layers.isEmpty()) {
            QPainter p(&img);
            for (const TextLayer &l : m_layers) {
                QFont f = p.font();
                f.setPointSize(l.size);
                f.setBold(true);
                p.setFont(f);
                p.setPen(l.color);
                p.drawText(QRect(l.x, l.y, TFT_W - l.x, TFT_H - l.y),
                           Qt::AlignLeft | Qt::AlignTop, l.text);
            }
        }
        drawn.append({ img, fr.second });
    }

    /* 2. Merge consecutive frames identical after RGB565 quantization,
          summing their delays into a single frame. */
    m_rendered.clear();
    for (const auto &fr : drawn) {
        QByteArray px = toRgb565(fr.first);
        if (!m_rendered.isEmpty() && toRgb565(m_rendered.last().first) == px) {
            m_rendered.last().second = qMin(m_rendered.last().second + fr.second,
                                            MAX_DELAY_MS);
        } else {
            m_rendered.append(fr);
        }
    }

    /* 3. Report dedup, then enforce the frame cap for upload. */
    QString msg;
    if (m_rendered.size() != drawn.size())
        msg = tr("%1 frames → %2 after dedup").arg(drawn.size()).arg(m_rendered.size());
    else
        msg = tr("%1 frame(s) ready").arg(m_rendered.size());
    int cap = maxFrames();
    if (m_rendered.size() > cap) {
        msg += tr(" — truncated to %1 (limit %2)").arg(cap).arg(cap);
        m_rendered.resize(cap);
    }
    m_status->setText(m_frames.isEmpty() ? tr("No frames imported.") : msg);
    m_status->setStyleSheet("");
    m_preview->setFrames(m_rendered);
}

void ScreenWidget::frameCapToggled() {
    renderFrames(); /* re-applies the cap to the imported frames */
}

/* ---------- text layers + undo ---------- */

void ScreenWidget::pushUndo() {
    m_undo.append(m_layers);
    if (m_undo.size() > 64) m_undo.removeFirst();
    m_undoLayer->setEnabled(true);
}

void ScreenWidget::refreshLayerList(int selectRow) {
    int prev = selectRow >= 0 ? selectRow : m_layerList->currentRow();
    m_layerList->blockSignals(true);
    m_layerList->clear();
    for (int i = 0; i < m_layers.size(); i++) {
        const TextLayer &l = m_layers[i];
        QString label = QString("%1: %2").arg(i + 1).arg(l.text);
        if (label.size() > 40) label = label.left(39) + "…";
        m_layerList->addItem(label);
    }
    m_layerList->blockSignals(false);
    if (!m_layers.isEmpty())
        m_layerList->setCurrentRow(qBound(0, prev, m_layers.size() - 1));
    else
        layerSelectionChanged();
    m_undoLayer->setEnabled(!m_undo.isEmpty());
}

void ScreenWidget::loadLayerIntoEditor(int row) {
    if (row < 0 || row >= m_layers.size()) return;
    const TextLayer &l = m_layers[row];
    m_layerText->setText(l.text);
    m_layerSize->setValue(l.size);
    m_layerX->setValue(l.x);
    m_layerY->setValue(l.y);
    m_layerColor = l.color;
    updateColorButton();
}

void ScreenWidget::layerSelectionChanged() {
    int row = m_layerList->currentRow();
    bool sel = row >= 0 && row < m_layers.size();
    m_editLayer->setEnabled(sel);
    m_delLayer->setEnabled(sel);
    if (sel) loadLayerIntoEditor(row);
}

TextLayer ScreenWidget::layerFromEditor() const {
    TextLayer l;
    l.text = m_layerText->text();
    l.size = m_layerSize->value();
    l.color = m_layerColor;
    l.x = m_layerX->value();
    l.y = m_layerY->value();
    return l;
}

void ScreenWidget::addLayer() {
    TextLayer l = layerFromEditor();
    if (l.text.isEmpty()) {
        m_status->setText(tr("Enter some text for the layer first."));
        return;
    }
    pushUndo();
    m_layers.append(l);
    refreshLayerList(m_layers.size() - 1);
    renderFrames();
}

void ScreenWidget::applyLayer() {
    int row = m_layerList->currentRow();
    if (row < 0 || row >= m_layers.size()) return;
    TextLayer l = layerFromEditor();
    if (l.text.isEmpty()) {
        m_status->setText(tr("Enter some text for the layer first."));
        return;
    }
    pushUndo();
    m_layers[row] = l;
    refreshLayerList(row);
    renderFrames();
}

void ScreenWidget::deleteLayer() {
    int row = m_layerList->currentRow();
    if (row < 0 || row >= m_layers.size()) return;
    pushUndo();
    m_layers.removeAt(row);
    refreshLayerList(qMin(row, m_layers.size() - 1));
    renderFrames();
}

void ScreenWidget::undoLayers() {
    if (m_undo.isEmpty()) return;
    m_layers = m_undo.takeLast();
    refreshLayerList();
    renderFrames();
}

void ScreenWidget::updateColorButton() {
    m_colorBtn->setStyleSheet(
        QString("background:%1; color:%2;")
            .arg(m_layerColor.name(),
                 QColor::fromRgb(m_layerColor.rgb() ^ 0xFFFFFF).name()));
}

/* ---------- upload ---------- */

QVector<uint8_t> ScreenWidget::encodeAnimation(
        const QVector<QPair<QImage, int>> &frames, int *blockCount, QString *err) const {
    *err = tr("No frames — import a GIF or image first.");
    if (frames.isEmpty()) return {};
    if (frames.size() > MAX_FRAMES_EXPERIMENTAL) {
        *err = tr("Too many frames (%1, limit %2).")
                   .arg(frames.size()).arg(MAX_FRAMES_EXPERIMENTAL);
        return {};
    }

    /* 256-byte header: N, delays in 2 ms units, 00, FF padding (official format). */
    QVector<uint8_t> out(HEADER_BYTES, 0xFF);
    out[0] = (uint8_t)frames.size();
    for (int i = 0; i < frames.size(); i++)
        out[1 + i] = (uint8_t)qBound(10, frames[i].second / 2, 255);
    out[frames.size() + 1 > HEADER_BYTES ? HEADER_BYTES - 1 : frames.size() + 1] = 0x00;

    for (const auto &fr : frames) {
        QByteArray px = toRgb565(fr.first);
        for (char c : px) out.append((uint8_t)c);
    }
    int chunks = (out.size() + CHUNK_BYTES - 1) / CHUNK_BYTES;
    out.resize(chunks * CHUNK_BYTES, 0xFF);
    *blockCount = chunks;
    *err = QString();
    return out;
}

bool ScreenWidget::uploadTft(const QVector<uint8_t> &payload, int slot, Device *dev,
                             QString *err,
                             const std::function<void(int, int)> &progress) {
    progress(0, 0); /* indeterminate: looking for the TFT interface */
    /* This board is "family B": TFT data on usage page 0xFF68 (4096-byte
       outputs + 64-byte acks) — verified descriptor:
       06 68ff 09 61 [09 62 … 81 02][09 63 … 96 0010 91 02] c0.
       Family A (web-output) boards use 0xFF67; try FF68 first, then FF67. */
    hid_init();
    hid_device *tft = nullptr;
    const struct { uint16_t page; const char *name; } pages[] = {
        { 0xFF68, "FF68" }, { 0xFF67, "FF67" },
    };
    for (const auto &pg : pages) {
        for (uint16_t pid : TFT_PIDS) {
            struct hid_device_info *list = hid_enumerate(VID, pid), *it;
            for (it = list; it; it = it->next) {
                if (it->usage_page == pg.page) { tft = hid_open_path(it->path); break; }
            }
            hid_free_enumeration(list);
            if (tft) break;
        }
        if (tft) break;
    }
    if (!tft) {
        *err = tr("TFT interface (0xFF68/0xFF67) not found");
        return false;
    }

    /* 1. Control session: START(0) → 04 72 <slot, chunks> … chunks … COMMIT.
       2. Chunks go as OUTPUT reports on the TFT interface with 64-byte acks. */
    bool ok = false;
    do {
        if (!dev->isConnected() && !dev->connectKeyboard()) { *err = dev->lastError(); break; }

        int chunks = payload.size() / CHUNK_BYTES;
        progress(0, chunks);
        if (!dev->tftBegin(slot, chunks)) {
            *err = dev->lastError();
            break;
        }
        ok = true;
        for (int i = 0; i < chunks && ok; i++) {
            unsigned char rep[CHUNK_BYTES + 1] = { 0 };
            memcpy(rep + 1, payload.constData() + i * CHUNK_BYTES, CHUNK_BYTES);
            if (hid_write(tft, rep, sizeof rep) != sizeof rep) {
                *err = tr("Chunk %1/%2 write failed").arg(i + 1).arg(chunks);
                ok = false;
                break;
            }
            unsigned char ack[64];
            int n = hid_read_timeout(tft, ack, sizeof ack, 1000);
            if (n <= 0) {
                *err = tr("No ack for chunk %1/%2").arg(i + 1).arg(chunks);
                ok = false;
                break;
            }
            progress(i + 1, chunks);
        }
        if (ok) {
            ok = dev->tftCommit();
            if (!ok) *err = dev->lastError();
        } else {
            /* close the session so the keyboard accepts further commands */
            dev->abortSession(0);
        }
    } while (false);

    hid_close(tft);
    return ok;
}

void ScreenWidget::setBusy(bool busy) {
    m_upload->setEnabled(!busy);
    m_import->setEnabled(!busy);
}

void ScreenWidget::uploadProgress(int done, int total) {
    if (total <= 0) {
        m_progress->setRange(0, 0);
        m_progress->setLabelText(tr("Opening TFT interface…"));
        return;
    }
    m_progress->setRange(0, total);
    m_progress->setValue(done);
    m_progress->setLabelText(tr("Sending chunk %1/%2…").arg(done).arg(total));
}

void ScreenWidget::upload() {
    int blocks = 0;
    QString err;
    QVector<uint8_t> payload = encodeAnimation(m_rendered, &blocks, &err);
    if (payload.isEmpty()) {
        m_status->setText(err);
        m_status->setStyleSheet("color:#e07070;");
        return;
    }

    const int slot = m_slot->value();
    const int frameCount = m_rendered.size();
    auto sharedErr = std::make_shared<QString>();
    QPointer<QObject> self(this);
    setBusy(true);
    m_status->setText(tr("Sending %1 blocks of 4096 bytes…").arg(blocks));
    m_status->setStyleSheet("");
    m_progress->reset();
    m_progress->setRange(0, 0);
    m_progress->setLabelText(tr("Opening TFT interface…"));
    m_progress->show();

    /* The whole transfer runs serialized on the HidWorker HID thread; only
       progress/completion are posted back to the GUI thread. */
    HidWorker::run(this,
        [payload, slot, sharedErr, self](Device *dev) mutable -> bool {
            auto progress = [self](int done, int total) {
                if (self)
                    QMetaObject::invokeMethod(self, "uploadProgress",
                                              Qt::QueuedConnection,
                                              Q_ARG(int, done), Q_ARG(int, total));
            };
            return uploadTft(payload, slot, dev, sharedErr.get(), progress);
        },
        [this, sharedErr, slot, frameCount](bool ok, QString devErr) {
            setBusy(false);
            m_progress->hide();
            if (ok) {
                m_status->setText(tr("Animation sent (slot %1, %2 frames). "
                                     "Press Fn + Delete to display it.")
                                      .arg(slot).arg(frameCount));
                m_status->setStyleSheet("color:#7ec97e;");
            } else {
                QString e = sharedErr->isEmpty() ? devErr : *sharedErr;
                m_status->setText(tr("Upload failed: %1").arg(e));
                m_status->setStyleSheet("color:#e07070;");
            }
        });
}

} /* namespace ak820 */

#include "screenwidget.moc"
