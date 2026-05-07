#pragma once

#include <QHash>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QWidget>

class QVBoxLayout;
class QTimer;

namespace quewi::audio { class AudioEngine; }
namespace quewi::core  { class Workspace; }

namespace quewi::ui {

// Compact panel that lists every currently-playing audio voice with a
// progress bar, gain readout, and a Stop button. Refreshes at 4 Hz off
// a QTimer — cheap snapshot from AudioEngine::activeVoices().
//
// Future expansion (Phase 6+): video voices, lighting fades, OSC sends
// in flight. The structure here makes that additive.
class ActiveCuesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ActiveCuesPanel(QWidget *parent = nullptr);
    ~ActiveCuesPanel() override;

    void setAudioEngine(audio::AudioEngine *engine);
    void setWorkspace(core::Workspace *workspace);

signals:
    // Emitted on each refresh tick. The set lists the cues whose voices
    // are currently sounding, so the cue list view can repaint state dots.
    void runningCueIdsChanged(const QSet<QUuid> &ids);
    // Per-cue stereo peaks for the inline VU meter in the cue list.
    // Emits at the same 4 Hz refresh cadence; the model debounces
    // dataChanged so the view doesn't repaint everything.
    void peakLevelsChanged(const QHash<QUuid, QPair<float, float>> &peaks);

private slots:
    void refresh();

private:
    class Row;

    Row *findOrCreateRow(quint64 voiceId);
    QString cueLabelForVoice(quint64 voiceId) const;

    QPointer<audio::AudioEngine> m_engine;
    QPointer<core::Workspace>    m_workspace;
    QVBoxLayout *m_rows = nullptr;
    QTimer      *m_timer = nullptr;
    QHash<quint64, Row *> m_rowMap;
};

} // namespace quewi::ui
