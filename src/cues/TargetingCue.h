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

// Pause: target audio/video cue stops advancing without resetting; a
// subsequent Start on the same target resumes from the pause point.
class PauseCue : public TargetingCue {
    Q_OBJECT
public:
    explicit PauseCue(QObject *parent = nullptr) : TargetingCue(parent) {}
    QString typeKey()  const override { return QStringLiteral("pause"); }
    QString typeName() const override { return tr("Pause"); }
};

// Load: pre-roll the target — open file handles, decode the audio head,
// realize the texture — without actually starting playback. Reduces
// GO-time latency for big media cues.
class LoadCue : public TargetingCue {
    Q_OBJECT
public:
    explicit LoadCue(QObject *parent = nullptr) : TargetingCue(parent) {}
    QString typeKey()  const override { return QStringLiteral("load"); }
    QString typeName() const override { return tr("Load"); }
};

// Reset: stop the target and rewind to its initial state. Useful for
// re-arming a cue mid-show without re-loading the file.
class ResetCue : public TargetingCue {
    Q_OBJECT
public:
    explicit ResetCue(QObject *parent = nullptr) : TargetingCue(parent) {}
    QString typeKey()  const override { return QStringLiteral("reset"); }
    QString typeName() const override { return tr("Reset"); }
};

// Devamp: tell a vamping fade/audio cue to break out of its loop and
// continue. No-op against non-vamping targets.
class DevampCue : public TargetingCue {
    Q_OBJECT
public:
    explicit DevampCue(QObject *parent = nullptr) : TargetingCue(parent) {}
    QString typeKey()  const override { return QStringLiteral("devamp"); }
    QString typeName() const override { return tr("Devamp"); }
};

} // namespace quewi::cues
