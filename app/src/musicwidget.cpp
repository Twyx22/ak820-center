/*
 * musicwidget.cpp — "Music Rhythm" engine (see musicwidget.h).
 *
 * Pipeline:
 *   QAudioSource pull model (QIODevice::readyRead, 44.1 kHz mono s16)
 *     -> latest 1024 samples, Hann window, 1024-point power-of-two DFT
 *        (iterative radix-2 FFT, no external libs)
 *     -> 8 log-ish magnitude bands (sub / bass / low-mid / mid / high-mid /
 *        high / brilliance / air), attack/decay smoothed
 *     -> selectable palette (Fire / Ocean / Spectrum)
 *     -> 8 horizontal zones of the 144-slot LED matrix (slots ordered by
 *        layout row/column; even split as fallback, see buildZoneMap)
 *     -> ~15 fps throttle (QTimer), fire-and-forget HidWorker pushes of
 *        Device::pushRealtimeFrame(), guarded by m_pushInFlight so the engine
 *        never queues up behind the HID thread.
 */
#include "musicwidget.h"

#include "device.h"
#include "hidworker.h"
#include "layout.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMediaDevices>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QIODevice>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>

namespace ak820 {
namespace {

constexpr int kFftSize = 1024;
constexpr int kBandCount = 8;
constexpr int kFrameIntervalMs = 66;            /* ~15 fps color pushes */
constexpr int kPcmKeepBytes = kFftSize * 2 * 4; /* keep at most 4 windows */
constexpr double kTwoPi = 6.283185307179586476925286766559;

/* Band edges in Hz: sub, bass, low-mid, mid, high-mid, high, brilliance, air. */
const float kBandEdges[kBandCount + 1] = {
    20.f, 60.f, 130.f, 300.f, 700.f, 1500.f, 3500.f, 8000.f, 16000.f
};

/* Iterative radix-2 FFT — a plain power-of-two DFT, computed in place. */
void fftRadix2(std::array<std::complex<double>, kFftSize> &a) {
    constexpr unsigned n = kFftSize;
    for (unsigned i = 1, j = 0; i < n; ++i) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (unsigned len = 2; len <= n; len <<= 1) {
        const double ang = -kTwoPi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (unsigned i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (unsigned j = 0; j < len / 2; ++j) {
                const std::complex<double> u = a[i + j];
                const std::complex<double> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

/* Selectable color palettes: band index + intensity (0..1) -> RGB. */
enum PaletteKind { PaletteFire = 0, PaletteOcean = 1, PaletteSpectrum = 2 };

QColor paletteColor(PaletteKind kind, int band, float t) {
    t = qBound(0.f, t, 1.f);
    switch (kind) {
    case PaletteFire:     /* black -> red -> orange -> pale yellow */
        return QColor::fromRgbF(qMin(1.f, t * 2.4f),
                                qBound(0.f, t * 2.4f - 0.9f, 0.9f),
                                qBound(0.f, t * 2.4f - 1.8f, 0.45f));
    case PaletteOcean:    /* black -> deep blue -> cyan -> icy white */
        return QColor::fromRgbF(qBound(0.f, t * 2.0f - 1.5f, 0.55f),
                                qBound(0.f, t * 2.0f - 0.4f, 0.85f),
                                qMin(1.f, t * 2.2f));
    case PaletteSpectrum: /* band index drives the hue, intensity the value */
        return QColor::fromHsvF(static_cast<float>(band) / kBandCount, 1.f, t);
    }
    return QColor::fromRgbF(t, t, t);
}

/* Painted 8-band spectrum visualization. */
class SpectrumView : public QWidget {
public:
    explicit SpectrumView(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(110);
        m_levels.fill(0.f);
    }
    void setPaletteKind(PaletteKind kind) { m_palette = kind; update(); }
    void setLevels(const std::array<float, kBandCount> &levels) {
        m_levels = levels;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(24, 25, 29));
        const QRectF area = QRectF(rect()).adjusted(10, 10, -10, -10);
        const float bw = static_cast<float>(area.width()) / kBandCount;
        for (int b = 0; b < kBandCount; ++b) {
            const float t = qBound(0.f, m_levels[b], 1.f);
            const float h = t * static_cast<float>(area.height());
            const QRectF bar(area.left() + b * bw + 3.f, area.bottom() - h,
                             bw - 6.f, h);
            p.setPen(Qt::NoPen);
            /* Floor the intensity so idle zones still show their color. */
            p.setBrush(paletteColor(m_palette, b, qMax(t, 0.12f)));
            p.drawRoundedRect(bar, 3, 3);
        }
    }
private:
    std::array<float, kBandCount> m_levels;
    PaletteKind m_palette = PaletteFire;
};

/* Engine + UI controller. Parented to the MusicWidget, so its destructor
 * runs during ~MusicWidget and shuts the engine down. All audio/HID callback
 * delivery happens in the GUI thread (widget thread). */
class MusicWidgetImpl : public QObject {
public:
    explicit MusicWidgetImpl(MusicWidget *widget) : QObject(widget) {
        m_zoneMap = buildZoneMap();
        m_pushReceiver = new QObject(this);
        buildUi(widget);

        m_timer = new QTimer(this);
        m_timer->setInterval(kFrameIntervalMs);
        connect(m_timer, &QTimer::timeout, this, [this] { onTick(); });

        m_mediaDevices = new QMediaDevices(this);
        connect(m_mediaDevices, &QMediaDevices::audioInputsChanged,
                this, [this] { refreshInputs(); });
        refreshInputs();
    }

    ~MusicWidgetImpl() override { shutdown(); }

    /* Called by the destructor path: cancel in-flight pushes and stop the
     * QAudioSource before the UI children are torn down. */
    void shutdown() {
        m_cancelling = true;
        if (m_timer) m_timer->stop();
        if (m_audio) {
            m_audio->stop();
            delete m_audio;
            m_audio = nullptr;
        }
        m_io.clear();
    }

private:
    void buildUi(MusicWidget *widget) {
        m_inputCombo = new QComboBox;
        m_paletteCombo = new QComboBox;
        m_paletteCombo->addItem(MusicWidget::tr("Fire"), PaletteFire);
        m_paletteCombo->addItem(MusicWidget::tr("Ocean"), PaletteOcean);
        m_paletteCombo->addItem(MusicWidget::tr("Spectrum"), PaletteSpectrum);
        connect(m_paletteCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            m_view->setPaletteKind(
                static_cast<PaletteKind>(m_paletteCombo->currentData().toInt()));
        });

        m_gain = new QSlider(Qt::Horizontal);
        m_gain->setRange(1, 64);
        m_gain->setValue(16);
        m_gain->setFixedWidth(160);
        m_gainValue = new QLabel(QString::number(m_gain->value()));
        connect(m_gain, &QSlider::valueChanged, this, [this](int v) {
            m_gainValue->setText(QString::number(v));
        });

        m_view = new SpectrumView;

        m_startStop = new QPushButton(MusicWidget::tr("Start"));
        m_startStop->setObjectName("accent");
        m_startStop->setMinimumHeight(42);
        m_startStop->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(m_startStop, &QPushButton::clicked, this, [this] {
            if (m_running) stopEngine(); else startEngine();
        });

        auto *inputHint = new QLabel(MusicWidget::tr(
            "PulseAudio/PipeWire monitor sources are listed — pick \"Monitor of …\" "
            "to make the keyboard react to system audio."));
        inputHint->setObjectName("hint");
        inputHint->setWordWrap(true);

        m_hint = new QLabel(MusicWidget::tr(
            "Experimental — requires the 2.4G dongle; not yet validated on hardware."));
        m_hint->setObjectName("hint");
        m_hint->setWordWrap(true);

        m_status = new QLabel;

        auto *inputRow = new QHBoxLayout;
        inputRow->addWidget(new QLabel(MusicWidget::tr("Audio input:")));
        inputRow->addWidget(m_inputCombo, 1);
        inputRow->addWidget(new QLabel(MusicWidget::tr("Palette:")));
        inputRow->addWidget(m_paletteCombo);

        auto *gainRow = new QHBoxLayout;
        gainRow->addWidget(new QLabel(MusicWidget::tr("Sensitivity:")));
        gainRow->addWidget(m_gain);
        gainRow->addWidget(m_gainValue);
        gainRow->addStretch(1);

        auto *root = new QVBoxLayout(widget);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(8);
        root->addLayout(inputRow);
        root->addWidget(inputHint);
        root->addLayout(gainRow);
        root->addWidget(m_view, 1);
        root->addWidget(m_startStop);
        root->addWidget(m_hint);
        root->addWidget(m_status);
    }

    /* Slots ordered by keyboard row/column, split into kBandCount contiguous
     * horizontal zones; slots missing from the layout XML stay dark (-1).
     * Fallback when the layout is unavailable: even split across indexes. */
    std::array<int, LED_COUNT> buildZoneMap() {
        std::array<int, LED_COUNT> zone;
        zone.fill(-1);
        QVector<const LayoutKey *> keys;
        for (const LayoutKey &k : Layout::instance()->keys())
            if (k.lightIndex >= 0 && k.lightIndex < LED_COUNT) keys.append(&k);
        if (keys.isEmpty()) {
            for (int s = 0; s < LED_COUNT; ++s)
                zone[s] = s * kBandCount / LED_COUNT;
            return zone;
        }
        std::sort(keys.begin(), keys.end(), [](const LayoutKey *a, const LayoutKey *b) {
            return a->row != b->row ? a->row < b->row : a->col < b->col;
        });
        for (int i = 0; i < keys.size(); ++i)
            zone[keys[i]->lightIndex] = i * kBandCount / keys.size();
        return zone;
    }

    void refreshInputs() {
        const QByteArray previous = m_inputCombo->currentData().toByteArray();
        m_inputCombo->clear();
        const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
        const QByteArray defaultId = QMediaDevices::defaultAudioInput().id();
        for (const QAudioDevice &d : inputs)
            m_inputCombo->addItem(d.description(), d.id());
        int select = m_inputCombo->findData(previous);
        if (select < 0) select = m_inputCombo->findData(defaultId);
        if (select >= 0) m_inputCombo->setCurrentIndex(select);
    }

    void startEngine() {
        if (m_running) return;
        m_status->clear();
        m_status->setStyleSheet(QString());

        QAudioDevice device;
        const QByteArray wanted = m_inputCombo->currentData().toByteArray();
        for (const QAudioDevice &d : QMediaDevices::audioInputs())
            if (d.id() == wanted) { device = d; break; }
        if (device.isNull()) device = QMediaDevices::defaultAudioInput();
        if (device.isNull()) {
            setError(MusicWidget::tr("No audio input device is available."));
            return;
        }

        QAudioFormat format;
        format.setSampleRate(44100);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format)) {
            setError(MusicWidget::tr("%1 does not support 16-bit 44.1 kHz mono capture.")
                         .arg(device.description()));
            return;
        }

        m_audio = new QAudioSource(device, format, this);
        m_io = m_audio->start();
        if (!m_io) {
            delete m_audio;
            m_audio = nullptr;
            setError(MusicWidget::tr("Failed to start audio capture on \"%1\".")
                         .arg(device.description()));
            return;
        }
        connect(m_io, &QIODevice::readyRead, this, [this] { onAudioReady(); });

        m_pcm.clear();
        m_levels.fill(0.f);
        m_running = true;
        m_cancelling = false;
        m_timer->start();
        m_startStop->setText(MusicWidget::tr("Stop"));
        m_view->setPaletteKind(
            static_cast<PaletteKind>(m_paletteCombo->currentData().toInt()));
        setOk(MusicWidget::tr("Listening to \"%1\" — streaming to the keyboard.")
                  .arg(device.description()));
    }

    void stopEngine() {
        if (!m_running) return;
        m_cancelling = true; /* a frame push may still be in flight */
        m_timer->stop();
        if (m_audio) {
            m_audio->stop();
            m_audio->deleteLater();
            m_audio = nullptr;
        }
        m_io.clear();
        m_pcm.clear();
        m_levels.fill(0.f);
        m_running = false;
        m_view->setLevels(m_levels);
        m_startStop->setText(MusicWidget::tr("Start"));
        setOk(MusicWidget::tr("Stopped."));
        /* m_cancelling stays set until the next startEngine(): the completion
         * of a frame pushed just before the stop must not touch the UI. */
    }

    void onAudioReady() {
        if (!m_io) return;
        m_pcm.append(m_io->readAll());
        if (m_pcm.size() > kPcmKeepBytes)
            m_pcm = m_pcm.right(kPcmKeepBytes);
    }

    void onTick() {
        if (!m_running) return;
        if (m_pcm.size() >= kFftSize * 2) {
            std::array<qint16, kFftSize> samples;
            std::memcpy(samples.data(),
                        m_pcm.constData() + m_pcm.size() - kFftSize * 2,
                        kFftSize * 2);
            analyze(samples.data());
        }
        m_view->setLevels(m_levels);
        pushFrame();
    }

    void analyze(const qint16 *samples) {
        std::array<std::complex<double>, kFftSize> spectrum;
        for (int i = 0; i < kFftSize; ++i) {
            const double w = 0.5 - 0.5 * std::cos(kTwoPi * i / double(kFftSize - 1));
            spectrum[i] = std::complex<double>(double(samples[i]) * w, 0.0);
        }
        fftRadix2(spectrum);

        const float gain = static_cast<float>(m_gain->value());
        for (int b = 0; b < kBandCount; ++b) {
            const int lo = qBound(1, static_cast<int>(std::lround(
                    kBandEdges[b] * kFftSize / 44100.0)), kFftSize / 2 - 1);
            const int hi = qBound(lo, static_cast<int>(std::lround(
                    kBandEdges[b + 1] * kFftSize / 44100.0)), kFftSize / 2 - 1);
            double sum = 0.0;
            for (int k = lo; k <= hi; ++k) sum += std::abs(spectrum[k]);
            /* Hann coherent gain is 0.5: scale back to ~sample amplitude. */
            const double levelRaw = (sum / (hi - lo + 1)) * 2.0 / kFftSize;
            float v = qBound(0.0, (levelRaw / 32768.0) * gain, 1.0);
            v = std::sqrt(v);
            m_levels[b] = qMax(v, m_levels[b] * 0.72f); /* fast attack, slow decay */
        }
    }

    void pushFrame() {
        if (!m_running || m_cancelling || m_pushInFlight) return;

        std::array<uint8_t, LED_COUNT * 3> frame{};
        const PaletteKind palette =
            static_cast<PaletteKind>(m_paletteCombo->currentData().toInt());
        for (int slot = 0; slot < LED_COUNT; ++slot) {
            const int band = m_zoneMap[slot];
            if (band < 0) continue; /* slot not described by the layout */
            const QColor c = paletteColor(palette, band, m_levels[band]);
            frame[slot * 3]     = static_cast<uint8_t>(c.red());
            frame[slot * 3 + 1] = static_cast<uint8_t>(c.green());
            frame[slot * 3 + 2] = static_cast<uint8_t>(c.blue());
        }

        m_pushInFlight = true; /* drop frames instead of queueing up */
        HidWorker::run(m_pushReceiver,
            [frame](Device *dev) { return dev->pushRealtimeFrame(frame.data()); },
            [this](bool ok, QString error) {
                m_pushInFlight = false;
                if (!ok && !m_cancelling)
                    setError(MusicWidget::tr("Real-time lighting push failed: %1")
                                 .arg(error));
            });
    }

    void setOk(const QString &text) {
        m_status->setText(text);
        m_status->setStyleSheet(QStringLiteral("color:#7ec97e;"));
    }
    void setError(const QString &text) {
        m_status->setText(text);
        m_status->setStyleSheet(QStringLiteral("color:#e07070;"));
    }

    /* UI */
    QComboBox *m_inputCombo = nullptr;
    QComboBox *m_paletteCombo = nullptr;
    QSlider *m_gain = nullptr;
    QLabel *m_gainValue = nullptr;
    SpectrumView *m_view = nullptr;
    QPushButton *m_startStop = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_status = nullptr;

    /* Engine */
    QTimer *m_timer = nullptr;
    QMediaDevices *m_mediaDevices = nullptr;
    QAudioSource *m_audio = nullptr;
    QPointer<QIODevice> m_io;
    QObject *m_pushReceiver = nullptr; /* HID completion receiver (child) */
    QByteArray m_pcm;
    std::array<float, kBandCount> m_levels;
    std::array<int, LED_COUNT> m_zoneMap;
    bool m_running = false;
    bool m_pushInFlight = false;
    bool m_cancelling = false;
};

} /* anonymous namespace */

MusicWidget::MusicWidget(QWidget *parent) : QWidget(parent) {
    new MusicWidgetImpl(this); /* impl parents all UI to this widget */
}

/* The MusicWidgetImpl child destructor runs right after this body: it sets
 * the cancelling flag, stops the timer and stops/deletes the QAudioSource,
 * so no engine callback can touch UI that is being torn down. An RGB frame
 * still queued on the HID thread is harmless (it is captured by value and
 * pushRealtimeFrame fails cleanly), and its completion is dropped because
 * the receiver QObject is destroyed together with this widget. */
MusicWidget::~MusicWidget() = default;

} /* namespace ak820 */
