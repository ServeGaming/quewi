#pragma once

#include "cues/Cue.h"

namespace quewi::cues {

// Common base for control cues that act on another cue by id —
// Start, Stop, Goto. Keeps the field/payload boilerplate in one place.
class TargetingCue : public Cue {
    Q_OBJECT
public:
    explicit TargetingCue(QObject *parent = nullptr);
    ~TargetingCue() override;

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    core::CueId targetId() const { return m_targetId; }

private:
    core::CueId m_targetId;
};

class StartCue : public TargetingCue {
    Q_OBJECT
public:
    explicit StartCue(QObject *parent = nullptr) : TargetingCue(parent) {}
    QString typeKey()  const override { return QStringLiteral("start"); }
    QString typeName() const override { return tr("Start"); }
};

class StopCue : public TargetingCue {
    Q_OBJECT
public:
    explicit StopCue(QObject *parent = nullptr) : TargetingCue(parent) {}
    QString typeKey()  const override { return QStringLiteral("stop"); }
    QString typeName() const override { return tr("Stop"); }
};

class GotoCue : public TargetingCue {
    Q_OBJECT
public:
    explicit GotoCue(QObject *parent = nullptr) : TargetingCue(parent) {}
    QString typeKey()  const override { return QStringLiteral("goto"); }
    QString typeName() const override { return tr("Goto"); }
};

} // namespace quewi::cues
