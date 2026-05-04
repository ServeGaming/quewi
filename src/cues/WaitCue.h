#pragma once

#include "cues/Cue.h"

namespace quewi::cues {

// Pure delay. Used inside groups and as a stand-alone "hold for N seconds"
// step in a list with auto-continue. Fires nothing; the GoEngine treats
// the wait duration as the cue's effective duration so post-wait stacks
// on top.
class WaitCue : public Cue {
    Q_OBJECT
public:
    explicit WaitCue(QObject *parent = nullptr);
    ~WaitCue() override;

    QString typeKey()  const override { return QStringLiteral("wait"); }
    QString typeName() const override { return tr("Wait"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    double durationSeconds() const { return m_durationSeconds; }

private:
    double m_durationSeconds = 1.0;
};

} // namespace quewi::cues
