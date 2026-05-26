#include "ui/ActiveCuesPanel.h"

#include "ui/Theme.h"
#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {
// N-channel peak meter with held-peak decay. Linear input (0..1+),
// drawn on a dB-mapped (-60..0 dBFS) gradient: green → yellow → red.
// Width is fixed; height grows with channel count so an Atmos cue
// (12 channels) becomes a tall ladder of bars rather than crushed
// pixels. The widget caps at 16 channels to match the engine.
class PeakMeter : public QWidget {
public:
    explicit PeakMeter(QWidget *parent = nullptr) : QWidget(parent)
    {
        setChannels(2);
    }
    void setChannels(int n) {
        n = std::clamp(n, 1, 16);
        // Resize the buffers whenever they don't already match — the
        // previous "if (n == m_channels) return" short-circuit treated
        // m_channels (default-initialised to 2) as proof the buffers
        // were sized, but the buffers start empty, so the first
        // setPeaks for a stereo cue read past the end of zero-length
        // m_disp / m_hold / m_age vectors.
        const bool resized = (m_channels != n)
                          || (int(m_disp.size()) != n);
        m_channels = n;
        if (int(m_disp.size()) != n) m_disp.assign(n, 0.f);
        if (int(m_hold.size()) != n) m_hold.assign(n, 0.f);
        if (int(m_age.size())  != n) m_age.assign(n, 0);
        if (!resized) return;
        // 7px per channel + 1px gaps; minimum ~18 px for stereo.
        const int h = std::max(18, n * 8 + 2);
        setFixedSize(48, h);
        update();
    }
    void setPeaks(const QList<float> &peaks) {
        if (peaks.isEmpty()) return;
        if (peaks.size() != m_channels) setChannels(peaks.size());
        for (int i = 0; i < m_channels && i < peaks.size(); ++i) {
            const float v = peaks[i];
            if (m_disp[i] < v) m_disp[i] = v;
            else m_disp[i] = std::max(v, m_disp[i] * 0.85f);
            if (v >= m_hold[i]) { m_hold[i] = v; m_age[i] = 0; }
            else if (++m_age[i] > 8) m_hold[i] = std::max(m_disp[i], m_hold[i] * 0.92f);
        }
        update();
    }
    // Backwards-compat for stereo callers.
    void setPeaks(float l, float r) {
        setPeaks(QList<float>{l, r});
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const int W = width(), H = height();
        p.fillRect(rect(), QColor(0x1F, 0x1D, 0x1B));
        const int totalH = H - 2;
        const int barH   = std::max(2, totalH / m_channels - 1);
        for (int i = 0; i < m_channels; ++i) {
            const int y = 1 + i * (barH + 1);
            const auto pos = [&](float lin) {
                if (lin <= 0.001f) return 0;
                float db = 20.f * std::log10(lin);
                if (db < -60.f) db = -60.f;
                if (db > 0.f) db = 0.f;
                return int((db + 60.f) / 60.f * (W - 2));
            };
            const int wDisp = pos(m_disp[i]);
            // Band thresholds in dBFS, matching CueRowDelegate. The
            // -60..0 dBFS scale → 0..W mapping puts -12 dB at t=0.8
            // and -3 dB at t=0.95. Earlier code used 0.7 / 0.9 which
            // tinted amber from ~-18 dB and red from ~-6 dB — same
            // peak read green in the cue list and amber here.
            for (int x = 0; x < wDisp; ++x) {
                float t = float(x) / float(W - 2);
                QColor c = (t < 0.8f)  ? QColor(0x6F, 0xAE, 0x63)   // green  < -12 dB
                          : (t < 0.95f) ? QColor(0xD7, 0xA2, 0x4E)  // amber  -12 .. -3 dB
                                        : QColor(0xC2, 0x6A, 0x55); // red    > -3 dB
                p.setPen(c);
                p.drawLine(1 + x, y, 1 + x, y + barH - 1);
            }
            int wHold = pos(m_hold[i]);
            if (wHold > 0) {
                p.setPen(QColor(0xE8, 0xE2, 0xD4));
                p.drawLine(1 + wHold, y, 1 + wHold, y + barH - 1);
            }
        }
    }
private:
    int m_channels = 2;
    std::vector<float> m_disp;
    std::vector<float> m_hold;
    std::vector<int>   m_age;
};
// Click-and-drag seekable progress bar. Behaves like a QProgressBar
// for display purposes (caller sets value/range) and like a QSlider
// for input — left-button press anywhere on the track snaps the
// playhead there and emits seekFractionRequested(0..1); held drags
// continue to emit so the operator can scrub. Hover shows a thin
// cursor highlight so the click target is obvious.
//
// Deliberately tiny — no track gradients, no shadows, no animation.
// The ACTIVE strip is high-information real estate and the bar is
// 8 px tall; anything fancier reads as visual noise.
class SeekBar : public QWidget {
public:
    explicit SeekBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(10);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setMinimumWidth(120);
    }
    // Set the callback invoked on click/drag. Single std::function
    // instead of a Q_OBJECT signal so this widget can live in an
    // anonymous namespace and the .cpp doesn't need an extra moc
    // include — keeps it consistent with the PeakMeter class above.
    void onSeek(std::function<void(double fraction)> cb) {
        m_seek = std::move(cb);
    }
    void setFraction(double f) {
        f = std::clamp(f, 0.0, 1.0);
        if (std::abs(f - m_fraction) < 1e-4) return;
        m_fraction = f;
        update();
    }
    void setBusy(bool b) {                  // for loops / unknown duration
        if (m_busy == b) return;
        m_busy = b;
        update();
    }
    void setEnabledLook(bool en) {
        setCursor(en ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_enabled = en;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        // Theme-token coloured so the bar reskins cleanly when the
        // operator switches dark / light / high-contrast.
        const auto &tk = quewi::ui::Theme::tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int H = height();
        const int trackY = (H - 4) / 2;
        // Track
        p.setPen(Qt::NoPen);
        p.setBrush(tk.bgRow);
        p.drawRoundedRect(QRectF(0, trackY, width(), 4), 2, 2);
        if (m_busy) {
            // Faint indeterminate stripe across the centre so the
            // operator still knows the row is alive.
            p.setBrush(tk.ink40);
            p.drawRoundedRect(QRectF(0, trackY + 1,
                                     width() * 0.35, 2), 1, 1);
            return;
        }
        // Filled portion — amber when scrubbable, muted when disabled
        // (loop / unknown duration).
        const int fillW = int(width() * m_fraction);
        p.setBrush(m_enabled ? tk.warn       // amber
                             : tk.ink40);    // muted
        p.drawRoundedRect(QRectF(0, trackY, fillW, 4), 2, 2);
        // Hover cursor line
        if (m_enabled && m_hoverX >= 0) {
            QColor hover = tk.ink100;
            hover.setAlpha(200);
            p.setPen(QPen(hover, 1));
            p.drawLine(m_hoverX, trackY - 1, m_hoverX, trackY + 5);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (!m_enabled || m_busy || e->button() != Qt::LeftButton) return;
        emitSeek(e->position().x());
        m_dragging = true;
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        m_hoverX = m_enabled ? int(e->position().x()) : -1;
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            emitSeek(e->position().x());
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        m_dragging = false;
    }
    void leaveEvent(QEvent *) override {
        m_hoverX = -1;
        update();
    }
private:
    void emitSeek(double x) {
        if (!m_seek) return;
        const double frac = std::clamp(x / std::max(1, width()), 0.0, 1.0);
        m_seek(frac);
    }
    double m_fraction = 0.0;
    bool   m_busy     = false;
    bool   m_enabled  = true;
    bool   m_dragging = false;
    int    m_hoverX   = -1;
    std::function<void(double)> m_seek;
};
} // namespace

namespace quewi::ui {

class ActiveCuesPanel::Row : public QWidget {
public:
    Row(quint64 voiceId, audio::AudioEngine *engine, QWidget *parent)
        : QWidget(parent), m_voiceId(voiceId), m_engine(engine)
    {
        setObjectName(QStringLiteral("activeCueRow"));
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(8);

        m_name = new QLabel(QStringLiteral("—"), this);
        m_name->setMinimumWidth(160);
        m_name->setStyleSheet(QStringLiteral("font-weight:600;"));

        m_bar = new SeekBar(this);
        // Translate "I clicked at fraction f" into "seek the voice to
        // f * durationSeconds". m_duration is updated every refresh
        // tick so this stays accurate as the voice plays through. A
        // loop or unknown-duration voice has m_duration <= 0 and the
        // bar disables itself (see update()).
        m_bar->onSeek([this](double frac) {
            if (!m_engine || m_duration <= 0.0) return;
            m_engine->seek(m_voiceId, frac * m_duration);
        });

        m_time = new QLabel(QStringLiteral("0:00 / 0:00"), this);
        m_time->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                  .arg(Theme::tokens().ink60.name()));
        m_time->setMinimumWidth(86);
        m_time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_meta = new QLabel(QStringLiteral(" "), this);
        m_meta->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                  .arg(Theme::tokens().ink60.name()));
        m_meta->setMinimumWidth(96);

        m_meter = new PeakMeter(this);

        m_stop = new QPushButton(tr("Stop"), this);
        m_stop->setObjectName(QStringLiteral("activeStopButton"));
        m_stop->setFixedWidth(64);
        connect(m_stop, &QPushButton::clicked, this, [this] {
            if (m_engine) m_engine->stop(m_voiceId, 0.1);
        });

        lay->addWidget(m_name, 1);
        lay->addWidget(m_bar, 2);
        lay->addWidget(m_time);
        lay->addWidget(m_meter);
        lay->addWidget(m_meta);
        lay->addWidget(m_stop);
    }

    void update(const audio::ActiveVoice &v, const QString &label)
    {
        m_name->setText(label);
        const double pos = v.positionSeconds;
        const double dur = v.durationSeconds;
        m_duration = dur;
        if (dur > 0.0) {
            m_bar->setBusy(false);
            m_bar->setEnabledLook(true);
            m_bar->setFraction(pos / dur);
            m_time->setText(QStringLiteral("%1 / %2").arg(formatTime(pos), formatTime(dur)));
        } else {
            // Loops and decode-in-flight voices have unknown duration —
            // seeking would land in the wrong place, so present the bar
            // as not-clickable rather than misleadingly draggable.
            m_bar->setBusy(true);
            m_bar->setEnabledLook(false);
            m_time->setText(formatTime(pos));
        }
        m_meta->setText(QStringLiteral("%1 dB · %2")
            .arg(QString::number(v.gainDb, 'f', 1),
                 panLabel(v.pan)));
        if (m_meter) {
            // Per-output meter when the engine reported per-channel
            // peaks (object-audio cues), stereo otherwise.
            if (!v.peakPerChannel.isEmpty() && v.peakPerChannel.size() > 2)
                m_meter->setPeaks(v.peakPerChannel);
            else
                m_meter->setPeaks(v.peakLeft, v.peakRight);
        }
    }

    quint64 voiceId() const { return m_voiceId; }

private:
    static QString formatTime(double seconds)
    {
        if (seconds < 0) seconds = 0;
        const int s = static_cast<int>(seconds);
        return QStringLiteral("%1:%2")
            .arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }
    static QString panLabel(double p)
    {
        if (qFuzzyIsNull(p)) return QStringLiteral("C");
        return p < 0 ? QStringLiteral("L%1").arg(QString::number(std::abs(p), 'f', 2))
                     : QStringLiteral("R%1").arg(QString::number(p, 'f', 2));
    }

    quint64 m_voiceId;
    audio::AudioEngine *m_engine;
    QLabel       *m_name = nullptr;
    SeekBar      *m_bar  = nullptr;
    QLabel       *m_time = nullptr;
    QLabel       *m_meta = nullptr;
    PeakMeter    *m_meter = nullptr;
    QPushButton  *m_stop = nullptr;
    double        m_duration = 0.0;
};

ActiveCuesPanel::ActiveCuesPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("activeCuesPanel"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 8, 12, 8);
    outer->setSpacing(4);

    auto *header = new QLabel(tr("ACTIVE"), this);
    header->setObjectName(QStringLiteral("activeCuesHeader"));
    outer->addWidget(header);

    m_rows = new QVBoxLayout();
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(2);
    outer->addLayout(m_rows);
    outer->addStretch(1);

    m_timer = new QTimer(this);
    m_timer->setInterval(33); // ~30 Hz so peak meters animate smoothly
    connect(m_timer, &QTimer::timeout, this, &ActiveCuesPanel::refresh);
    m_timer->start();

    setMinimumHeight(72);
}

ActiveCuesPanel::~ActiveCuesPanel() = default;

void ActiveCuesPanel::setAudioEngine(audio::AudioEngine *engine)
{
    m_engine = engine;
}

void ActiveCuesPanel::setWorkspace(core::Workspace *workspace)
{
    m_workspace = workspace;
}

QString ActiveCuesPanel::cueLabelForVoice(quint64 voiceId) const
{
    if (!m_workspace) return tr("Voice %1").arg(voiceId);
    auto *list = m_workspace->activeCueList();
    if (!list) return tr("Voice %1").arg(voiceId);
    for (int row = 0; row < list->cueCount(); ++row) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(row))) {
            if (ac->currentVoiceId() == voiceId) {
                const auto name = ac->name().isEmpty() ? ac->typeName() : ac->name();
                return QStringLiteral("%1  %2")
                    .arg(QString::number(ac->number(), 'f', 2), name);
            }
        }
    }
    return tr("Voice %1").arg(voiceId);
}

ActiveCuesPanel::Row *ActiveCuesPanel::findOrCreateRow(quint64 voiceId)
{
    auto it = m_rowMap.find(voiceId);
    if (it != m_rowMap.end()) return it.value();
    auto *r = new Row(voiceId, m_engine.data(), this);
    m_rows->addWidget(r);
    m_rowMap.insert(voiceId, r);
    return r;
}

void ActiveCuesPanel::refresh()
{
    if (!m_engine) return;
    const auto voices = m_engine->activeVoices();

    // Idle fast-path: nothing playing, nothing to clean up. Skip the
    // workspace walk and the signal emissions entirely so the 30 Hz
    // tick costs ~zero when the show is sitting still. The CueListModel
    // also bails on empty-then-empty in setPeakLevels, but stopping
    // here is even cheaper and avoids hover-repaint stutter.
    if (voices.isEmpty() && m_rowMap.isEmpty()) return;

    QSet<quint64> live;
    QSet<QUuid>   runningCueIds;
    QHash<QUuid, QPair<float, float>> peakByCue;
    for (const auto &v : voices) {
        live.insert(v.id);
        auto *row = findOrCreateRow(v.id);
        row->update(v, cueLabelForVoice(v.id));
        // Map voice → cue id for the cue-list state column + meters.
        if (m_workspace) {
            auto *list = m_workspace->activeCueList();
            if (list) {
                for (int i = 0; i < list->cueCount(); ++i) {
                    if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(i))) {
                        if (ac->currentVoiceId() == v.id) {
                            runningCueIds.insert(ac->id());
                            peakByCue.insert(ac->id(),
                                             { v.peakLeft, v.peakRight });
                            break;
                        }
                    }
                }
            }
        }
    }
    emit runningCueIdsChanged(runningCueIds);
    emit peakLevelsChanged(peakByCue);

    // Remove rows whose voices have ended.
    QList<quint64> toDrop;
    for (auto it = m_rowMap.constBegin(); it != m_rowMap.constEnd(); ++it) {
        if (!live.contains(it.key())) toDrop.append(it.key());
    }
    for (auto id : toDrop) {
        if (auto *r = m_rowMap.take(id)) r->deleteLater();
    }

    setVisible(!m_rowMap.isEmpty());
}

} // namespace quewi::ui
