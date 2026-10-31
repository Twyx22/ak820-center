#include "screenwidget.h"

#include <hidapi/hidapi.h>

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QVBoxLayout>

namespace ak820 {

static const uint16_t VID = 0x0C45;
static const uint16_t TFT_PIDS[] = { 0x8009, 0x800A };
static constexpr int TFT_W = 128, TFT_H = 128;
static constexpr int FRAME_BYTES = TFT_W * TFT_H * 2;      /* RGB565 */
static constexpr int HEADER_BYTES = 256;                    /* gif_headlength */
static constexpr int CHUNK_BYTES = 4096;
static constexpr int MAX_FRAMES = 30;                       /* verified legacy limit */

ScreenWidget::ScreenWidget(QWidget *parent) : QWidget(parent) {
    m_import = new QPushButton("Import GIF / images (resized to 128×128)…");
    m_previewBtn = new QPushButton("Convert & preview");
    m_upload = new QPushButton("Send to screen");
    m_upload->setObjectName("accent");
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_preview = new QLabel;
    m_preview->setFixedSize(TFT_W * 2, TFT_H * 2);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet("background:#0d0d10; border:1px solid #3a3d47; border-radius:8px;");
    m_hint = new QLabel("The animation is stored in the selected slot. "
                        "Press Fn + Delete on the keyboard to switch between the clock and your animation.");
    m_hint->setObjectName("hint");
    m_hint->setWordWrap(true);

    m_slot = new QSpinBox;
    m_slot->setRange(1, 3);
    m_slot->setValue(1);

    auto *slotRow = new QHBoxLayout;
    slotRow->addWidget(new QLabel("Destination slot:"));
    slotRow->addWidget(m_slot);
    slotRow->addStretch(1);

    auto *btns = new QHBoxLayout;
    btns->addWidget(m_import);
    btns->addWidget(m_previewBtn);
    btns->addWidget(m_upload);
    btns->addStretch(1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    root->addLayout(btns);
    root->addLayout(slotRow);
    root->addWidget(m_preview, 0, Qt::AlignCenter);
    root->addWidget(m_status);
    root->addWidget(m_hint);
    root->addStretch(1);

    connect(m_import, &QPushButton::clicked, this, &ScreenWidget::importFiles);
    connect(m_previewBtn, &QPushButton::clicked, this, &ScreenWidget::preview);
    connect(m_upload, &QPushButton::clicked, this, &ScreenWidget::upload);
}

void ScreenWidget::importFiles() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Images or GIF", QString(),
        "Images (*.gif *.png *.jpg *.jpeg *.bmp)");
    if (files.isEmpty()) return;
    m_frames.clear();
    for (const QString &f : files) {
        QImageReader rd(f);
        if (rd.imageCount() > 1 || f.endsWith(".gif", Qt::CaseInsensitive)) {
            int i = 0;
            while (i < MAX_FRAMES) {
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
        if (m_frames.size() >= MAX_FRAMES) break;
    }
    m_status->setText(QString("%1 frame(s) imported.").arg(m_frames.size()));
    preview();
}

void ScreenWidget::preview() {
    if (m_frames.isEmpty()) return;
    m_preview->setPixmap(QPixmap::fromImage(m_frames[0].first.scaled(
        TFT_W * 2, TFT_H * 2, Qt::KeepAspectRatio)));
}

QVector<uint8_t> ScreenWidget::encodeAnimation(
        const QVector<QPair<QImage, int>> &frames, int *blockCount, QString *err) const {
    *err = "no frames";
    if (frames.isEmpty() || frames.size() > MAX_FRAMES) return {};

    /* 256-byte header: N, delays in 2 ms units, 00, FF padding (legacy format). */
    QVector<uint8_t> out(HEADER_BYTES, 0xFF);
    out[0] = (uint8_t)frames.size();
    for (int i = 0; i < frames.size(); i++)
        out[1 + i] = (uint8_t)qBound(10, frames[i].second / 2, 255);
    out[frames.size() + 1 > HEADER_BYTES ? HEADER_BYTES - 1 : frames.size() + 1] = 0x00;

    for (const auto &fr : frames) {
        QImage img = fr.first.convertToFormat(QImage::Format_RGB888);
        for (int y = 0; y < TFT_H; y++) {
            const uchar *line = img.constScanLine(y);
            for (int x = 0; x < TFT_W; x++) {
                uint8_t r = line[x * 3], g = line[x * 3 + 1], b = line[x * 3 + 2];
                uint16_t px = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
                out.append(px & 0xff);
                out.append(px >> 8);
            }
        }
    }
    int chunks = (out.size() + CHUNK_BYTES - 1) / CHUNK_BYTES;
    out.resize(chunks * CHUNK_BYTES, 0xFF);
    *blockCount = chunks;
    *err = QString();
    return out;
}

bool ScreenWidget::uploadTft(const QVector<uint8_t> &payload, QString *err) {
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
        if (tft) {
            m_status->setText(QString("TFT interface %1 opened…").arg(pg.name));
            QApplication::processEvents();
            break;
        }
    }
    if (!tft) {
        *err = "TFT interface (0xFF68/0xFF67) not found";
        return false;
    }

    /* 1. Control session: START(0) → 04 72 <slot, chunks> … chunks … COMMIT.
       2. Chunks go as OUTPUT reports on the TFT interface with 64-byte acks. */
    Device *d = Device::instance();
    bool ok = false;
    do {
        if (!d->isConnected() && !d->connectKeyboard()) { *err = d->lastError(); break; }

        int chunks = payload.size() / CHUNK_BYTES;
        if (!d->tftBegin(m_slot->value(), chunks)) {
            *err = d->lastError();
            break;
        }
        ok = true;
        for (int i = 0; i < chunks && ok; i++) {
            unsigned char rep[CHUNK_BYTES + 1] = { 0 };
            memcpy(rep + 1, payload.constData() + i * CHUNK_BYTES, CHUNK_BYTES);
            if (hid_write(tft, rep, sizeof rep) != sizeof rep) {
                *err = QString("chunk %1/%2 write failed").arg(i + 1).arg(chunks);
                ok = false;
                break;
            }
            unsigned char ack[64];
            int n = hid_read_timeout(tft, ack, sizeof ack, 1000);
            if (n <= 0) {
                *err = QString("no ack for chunk %1/%2").arg(i + 1).arg(chunks);
                ok = false;
                break;
            }
            if (i % 4 == 0) {
                m_status->setText(QString("Sending chunk %1/%2…").arg(i + 1).arg(chunks));
                QApplication::processEvents();
            }
        }
        if (ok) {
            ok = d->tftCommit();
            if (!ok) *err = d->lastError();
        } else {
            /* close the session so the keyboard accepts further commands */
            d->abortSession(0);
        }
    } while (false);

    hid_close(tft);
    return ok;
}

void ScreenWidget::upload() {
    int blocks = 0;
    QString err;
    QVector<uint8_t> payload = encodeAnimation(m_frames, &blocks, &err);
    if (payload.isEmpty()) {
        m_status->setText(err);
        m_status->setStyleSheet("color:#e07070;");
        return;
    }
    m_status->setText(QString("Sending %1 blocks of 4096 bytes…").arg(blocks));
    m_status->setStyleSheet("");
    QApplication::processEvents();
    if (uploadTft(payload, &err)) {
        m_status->setText(QString("Animation sent (slot %1, %2 frames). "
                                  "Press Fn + Delete to display it.")
                              .arg(m_slot->value()).arg(m_frames.size()));
        m_status->setStyleSheet("color:#7ec97e;");
    } else {
        m_status->setText(err);
        m_status->setStyleSheet("color:#e07070;");
    }
}

} /* namespace ak820 */
