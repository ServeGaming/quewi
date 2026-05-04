#pragma once

#include "core/Workspace.h"
#include "cues/Cue.h"

namespace quewi::cues {

// A control cue that animates a parameter on a target cue over a
// duration. Phase 3 supports parameter == "gainDb" against an audio
// target. Geometry/opacity fades on video targets land in Phase 5,
// at which point this class grows the supported parameter set.
//
// Dispatch happens in MainWindow's GO handler — FadeCue itself doesn't
// know about engines so it stays in the core library cleanly.
class FadeCue : public Cue {
    Q_OBJECT
public:
    explicit FadeCue(QObject *parent = nullptr);
    ~FadeCue() override;

    QString typeKey()  const override { return QStringLiteral("fade"); }
    QString typeName() const override { return tr("Fade"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    core::CueId targetId()         const { return m_targetId; }
    QString     parameter()        const { return m_parameter; }
    double      targetValue()      const { return m_targetValue; }
    double      durationSeconds()  const { return m_durationSeconds; }

private:
    core::CueId m_targetId;
    QString     m_parameter       = QStringLiteral("gainDb");
    double      m_targetValue     = -60.0;     // dB by default
    double      m_durationSeconds = 3.0;
};

} // namespace quewi::cues
