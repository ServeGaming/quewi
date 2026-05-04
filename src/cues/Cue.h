#pragma once

#include "core/Workspace.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

namespace quewi::cues {

enum class ContinueMode {
    DoNotContinue = 0,
    AutoContinue,   // when this cue's post-wait elapses, fire next
    AutoFollow,     // when this cue starts, fire next immediately
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
};

} // namespace quewi::cues
