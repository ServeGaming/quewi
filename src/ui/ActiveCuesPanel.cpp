#include "ui/ActiveCuesPanel.h"

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
        m_time->setStyleSheet(QStringLiteral("color:#A8AEBA; font-size:11px;"));
        m_time->setMinimumWidth(86);
        m_time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_meta = new QLabel(QStringLiteral(" "), this);
        m_meta->setStyleSheet(QStringLiteral("color:#A8AEBA; font-size:11px;"));
        m_meta->setMinimumWidth(96);

        m_stop = new QPushButton(tr("Stop"), this);
        m_stop->setObjectName(QStringLiteral("activeStopButton"));
        m_stop->setFixedWidth(64);
        connect(m_stop, &QPushButton::clicked, this, [this] {
            if (m_engine) m_engine->stop(m_voiceId, 0.1);
        });

        lay->addWidget(m_name, 1);
        lay->addWidget(m_bar, 2);
        lay->addWidget(m_time);
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
    m_timer->setInterval(250);
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
    for (const auto &v : voices) {
        live.insert(v.id);
        auto *row = findOrCreateRow(v.id);
        row->update(v, cueLabelForVoice(v.id));
    }

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
