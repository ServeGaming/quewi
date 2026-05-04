#include "ui/ActiveCuesPanel.h"

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
// Small two-channel peak meter with held-peak decay. Linear input (0..1+),
// drawn on a dB-mapped (-60..0 dBFS) gradient: green → yellow → red.
class PeakMeter : public QWidget {
public:
    explicit PeakMeter(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(48, 18);
    }
    void setPeaks(float l, float r) {
        m_targetL = l; m_targetR = r;
        // Smooth attack (snap up), slow release.
        if (m_dispL < l) m_dispL = l; else m_dispL = std::max(l, m_dispL * 0.85f);
        if (m_dispR < r) m_dispR = r; else m_dispR = std::max(r, m_dispR * 0.85f);
        if (l >= m_holdL) { m_holdL = l; m_holdAgeL = 0; }
        else if (++m_holdAgeL > 8) m_holdL = std::max(m_dispL, m_holdL * 0.92f);
        if (r >= m_holdR) { m_holdR = r; m_holdAgeR = 0; }
        else if (++m_holdAgeR > 8) m_holdR = std::max(m_dispR, m_holdR * 0.92f);
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const int W = width(), H = height();
        const int barH = (H - 2) / 2;
        p.fillRect(rect(), QColor(0x10, 0x14, 0x19)); // bgDeep
        auto draw = [&](int y, float disp, float hold) {
            const auto pos = [&](float lin) {
                if (lin <= 0.001f) return 0;
                float db = 20.f * std::log10(lin);
                if (db < -60.f) db = -60.f;
                if (db > 0.f) db = 0.f;
                return int((db + 60.f) / 60.f * (W - 2));
            };
            const int wDisp = pos(disp);
            for (int x = 0; x < wDisp; ++x) {
                float t = float(x) / float(W - 2);
                QColor c = (t < 0.7f) ? QColor(0x60, 0xdf, 0x85)   // running
                          : (t < 0.9f) ? QColor(0xff, 0xb8, 0x69)  // warn
                                       : QColor(0xff, 0xb4, 0xab); // err
                p.setPen(c);
                p.drawLine(1 + x, y, 1 + x, y + barH - 1);
            }
            int wHold = pos(hold);
            if (wHold > 0) {
                p.setPen(QColor(0xe0, 0xe2, 0xeb)); // ink100 held-peak tick
                p.drawLine(1 + wHold, y, 1 + wHold, y + barH - 1);
            }
        };
        draw(1,           m_dispL, m_holdL);
        draw(2 + barH,    m_dispR, m_holdR);
    }
private:
    float m_targetL = 0, m_targetR = 0;
    float m_dispL = 0, m_dispR = 0;
    float m_holdL = 0, m_holdR = 0;
    int   m_holdAgeL = 0, m_holdAgeR = 0;
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

        m_bar = new QProgressBar(this);
        m_bar->setRange(0, 1000);
        m_bar->setValue(0);
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(8);

        m_time = new QLabel(QStringLiteral("0:00 / 0:00"), this);
        m_time->setStyleSheet(QStringLiteral("color:#c0c7d4; font-size:11px;"));
        m_time->setMinimumWidth(86);
        m_time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_meta = new QLabel(QStringLiteral(" "), this);
        m_meta->setStyleSheet(QStringLiteral("color:#c0c7d4; font-size:11px;"));
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
        if (dur > 0.0) {
            const int p = std::clamp(static_cast<int>(pos / dur * 1000.0), 0, 1000);
            m_bar->setRange(0, 1000);
            m_bar->setValue(p);
            m_time->setText(QStringLiteral("%1 / %2").arg(formatTime(pos), formatTime(dur)));
        } else {
            m_bar->setRange(0, 0); // busy indicator for loops/unknown
            m_time->setText(formatTime(pos));
        }
        m_meta->setText(QStringLiteral("%1 dB · %2")
            .arg(QString::number(v.gainDb, 'f', 1),
                 panLabel(v.pan)));
        if (m_meter) m_meter->setPeaks(v.peakLeft, v.peakRight);
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
    QProgressBar *m_bar  = nullptr;
    QLabel       *m_time = nullptr;
    QLabel       *m_meta = nullptr;
    PeakMeter    *m_meter = nullptr;
    QPushButton  *m_stop = nullptr;
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

    QSet<quint64> live;
    QSet<QUuid>   runningCueIds;
    for (const auto &v : voices) {
        live.insert(v.id);
        auto *row = findOrCreateRow(v.id);
        row->update(v, cueLabelForVoice(v.id));
        // Map voice → cue id for the cue-list state column.
        if (m_workspace) {
            auto *list = m_workspace->activeCueList();
            if (list) {
                for (int i = 0; i < list->cueCount(); ++i) {
                    if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(i))) {
                        if (ac->currentVoiceId() == v.id) {
                            runningCueIds.insert(ac->id());
                            break;
                        }
                    }
                }
            }
        }
    }
    emit runningCueIdsChanged(runningCueIds);

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
