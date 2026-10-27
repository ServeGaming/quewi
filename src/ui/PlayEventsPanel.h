#pragma once

#include "audio/AudioCue.h"
#include "audio/PlayEvents.h"

#include <QList>
#include <QPointer>
#include <QUuid>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <optional>

class QLabel;
class QPushButton;
class QTableWidget;
class QUndoStack;

namespace quewi::ui {

// The audio editor's "Vamps" tab: the song's vamps, repeats, holds, skips and
// markers (the cue's undoable field "playEvents") as a table you edit in
// place, plus "+ Vamp" … "+ Marker" buttons. The timeline's VAMPS lane sends
// its gestures here too, so this is the one write path: every edit is one
// undo step on the show's undo stack (or a plain setField without one).
//
// Kind keys used across the API, the same as the lane's: "vamp", "repeat",
// "hold", "skip", "marker".
class PlayEventsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlayEventsPanel(QWidget *parent = nullptr);

    void setCue(audio::AudioCue *cue);
    audio::AudioCue *cue() const { return m_cue; }
    void setUndoStack(QUndoStack *undo) { m_undo = undo; }

    // Where "+ Vamp" … land: the preview's playhead while it plays (the
    // provider returns < 0 when stopped), else the edit cursor.
    void setCursorSeconds(double s) { m_cursor = std::max(0.0, s); }
    void setPlayheadProvider(std::function<double()> f) { m_playhead = std::move(f); }
    // A stretch of the timeline the buttons should cover instead of a bar
    // (false = none).
    void setSpanProvider(std::function<bool(double &, double &)> f) { m_span = std::move(f); }
    // With the beat grid set and snap on, the buttons start on the nearest beat.
    void setSnapToBeats(bool on) { m_snap = on; }

    // The questions "Repeat…", "Marker…" and "Rename…" ask. Tests replace
    // them; the defaults are small input dialogs. Return nullopt = cancelled.
    void setCountAsker(std::function<std::optional<int>(int current)> f) { m_askCount = std::move(f); }
    void setNameAsker(std::function<std::optional<QString>(const QString &title, const QString &current)> f)
    {
        m_askName = std::move(f);
    }

    QUuid selectedEvent() const { return m_selected; }
    void  selectEvent(const QUuid &id);

    // One bar of the cue's beat grid; 2 s without one.
    double defaultLength() const;
    // "bar 3 beat 1" for a time, when the cue has a beat grid (else empty).
    QString barBeatText(double seconds) const;

    // ── Edits: each is one undo step. Refusals (an overlapping vamp, a band
    //    too short) leave the cue alone and say why in problem(). ─────────
    // end < 0 = the default length from start (ignored for a hold / marker).
    QUuid addEvent(const QString &kind, double start, double end = -1.0,
                   int count = 0, const QString &name = {});
    // From the lane or its menu: "repeat" asks how many times, "marker" asks
    // for its text, then addEvent.
    QUuid addFromLane(const QString &kind, double start, double end);
    // The "+" buttons: at the playhead (or over the span), asking as the lane does.
    QUuid addHere(const QString &kind);
    bool  moveEvent(const QUuid &id, double start, double end);
    // "makeVamp", "makeRepeat" (asks the count), "makeSkip", "makeHold",
    // "makeMarker", "exitEndOfPass", "exitNextBar", "exitNow", "rename"
    // (asks), "delete".
    bool  applyEventAction(const QUuid &id, const QString &action);
    bool  deleteEvent(const QUuid &id);
    // Field edits from the table (also used by tests).
    bool  setKind(const QUuid &id, const QString &kind, int count = 0);
    bool  setName(const QUuid &id, const QString &name);
    bool  setCount(const QUuid &id, int count);
    bool  setExit(const QUuid &id, audio::PlayEvent::Exit exit);

    QString problem() const;

signals:
    void selectionChanged(QUuid id);
    // Double-click a row: put the editor's cursor there.
    void seekRequested(double seconds);
    void edited();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The one write path: normalises, refuses if that would drop an event,
    // pushes one undo step.
    bool commit(audio::PlayEvents next, const QString &undoText);
    int  indexOf(const QUuid &id) const;
    bool editOne(const QUuid &id, const QString &undoText,
                 const std::function<void(audio::PlayEvent &)> &f);
    void setProblem(const QString &text);
    void scheduleRefresh();
    void refresh();
    double hereSeconds() const;
    static audio::PlayEvent::Kind kindOf(const QString &key);
    static QString keyOf(const audio::PlayEvent &e);

    QPointer<audio::AudioCue> m_cue;
    QUndoStack *m_undo = nullptr;
    double m_cursor = 0.0;
    bool   m_snap = false;
    std::function<double()> m_playhead;
    std::function<bool(double &, double &)> m_span;
    std::function<std::optional<int>(int)> m_askCount;
    std::function<std::optional<QString>(const QString &, const QString &)> m_askName;
    QUuid  m_selected;
    QList<QUuid> m_rowIds;
    bool   m_refreshQueued = false;
    bool   m_selecting = false;

    QLabel       *m_problem = nullptr;
    QPushButton  *m_deleteBtn = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace quewi::ui
