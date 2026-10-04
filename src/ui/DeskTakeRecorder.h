#pragma once

#include "audio/DeskRecording.h"

#include <QObject>
#include <QPointer>

#include <functional>

namespace quewi::osc { class EosFeedback; }

namespace quewi::ui {

// Records lighting triggers from the desk: listens to the Eos read-back link
// for cues the operator fires and notes each against a song's playhead. Used
// by the audio editor (its preview) and by the OSC remote (a cue playing for
// real). Read-only: it never sends anything to the desk.
class DeskTakeRecorder : public QObject {
    Q_OBJECT
public:
    explicit DeskTakeRecorder(QObject *parent = nullptr);

    void setFeedback(osc::EosFeedback *fb);
    // Where the song is now, in file seconds; < 0 = not playing (cues fired
    // then are left out — a paused or stopped song has no "when").
    void setPlayhead(std::function<double()> fn) { m_playhead = std::move(fn); }

    // Can it record now? Empty = yes; else why not, in plain English.
    QString whyNot() const;
    bool start(const QString &list = {});    // false (and whyNot()) if it can't
    void stop();
    bool isRecording() const { return m_take.isRecording(); }
    const std::vector<audio::RecordedCue> &takes() const { return m_take.takes(); }
    void clear() { m_take.clear(); }

signals:
    void recorded(const quewi::audio::RecordedCue &cue);   // one more in the take
    void recordingChanged(bool on);

private:
    void onFired(const QString &list, const QString &cue, const QString &label);

    QPointer<osc::EosFeedback> m_fb;
    std::function<double()> m_playhead;
    audio::DeskRecording m_take;
};

} // namespace quewi::ui
