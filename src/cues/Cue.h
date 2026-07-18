#pragma once

#include "core/Workspace.h"

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

namespace quewi::cues {

// Numeric values are persisted in show files and the Inspector combo — keep
// them stable. Semantics follow QLab:
enum class ContinueMode {
    DoNotContinue = 0,
    AutoContinue,   // on GO, fire the NEXT cue immediately (after pre-wait)
    AutoFollow,     // fire the next cue only AFTER this cue's action finishes
                    // (audio/video reaches its end, or the duration elapses),
                    // then this cue's post-wait
};

// Polymorphic base for every cue type. Subclasses register with a
// CueRegistry at startup (Phase 6) — for now the few built-ins are
// constructed directly.
//
// Field-style edits go through setField()/field() so the EditCueFieldCommand
// undo command can route any editable parameter generically.
class Cue : public QObject {
    Q_OBJECT
public:
    explicit Cue(QObject *parent = nullptr);
    ~Cue() override;

    // Identity & type
    core::CueId id() const { return m_id; }
    void setId(core::CueId id) { m_id = id; } // for ShowFile load
    virtual QString typeKey() const = 0;   // "memo", "audio", "osc", …
    virtual QString typeName() const = 0;  // human-readable

    // Common fields
    double  number() const         { return m_number; }
    QString name() const           { return m_name; }
    double  preWait() const        { return m_preWait; }
    double  postWait() const       { return m_postWait; }
    ContinueMode continueMode() const { return m_continueMode; }
    QString notes() const          { return m_notes; }
    bool    isArmed() const        { return m_armed; }
    QColor  color() const          { return m_color; } // invalid = no tint

    // Cross-list link (QLab-style). A cue may point at one other cue — the
    // common use is pairing a sound cue with a DCA (mix) cue so one GO fires
    // both. Firing is bidirectional: firing this cue fires the linked one, and
    // firing the linked one fires this (resolved by reverse lookup). Null = no
    // link. The firing coordination + loop-guard live in MainWindow, which is
    // the only place that owns both the GoEngine and the console link.
    core::CueId linkedCueId() const { return m_linkedCueId; }

    // Generic accessor — returns invalid QVariant for unknown fields.
    virtual QVariant field(const QString &key) const;
    // Generic mutator — silently ignores unknown fields. Emits changed().
    virtual void setField(const QString &key, const QVariant &value);

    // Persistence — type-specific subclasses extend payload().
    virtual QJsonObject toPayload() const;
    virtual void fromPayload(const QJsonObject &payload);

signals:
    // Row in the owning CueList changed; the list re-emits via cueChanged.
    void changed();

protected:
    void emitChanged();

private:
    core::CueId  m_id;
    double       m_number = 0.0;
    QString      m_name;
    double       m_preWait = 0.0;
    double       m_postWait = 0.0;
    ContinueMode m_continueMode = ContinueMode::DoNotContinue;
    QString      m_notes;
    bool         m_armed = true;
    QColor       m_color;        // invalid by default → no tint
    core::CueId  m_linkedCueId;  // null by default → no cross-list link
};

} // namespace quewi::cues
