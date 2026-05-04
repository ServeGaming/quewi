#pragma once

#include "cues/Cue.h"

#include <QHash>
#include <QVariantMap>

namespace quewi::lighting {

// A Light cue applies a sparse set of (channel → 0..255) values to a
// single universe. Channels that aren't in the map are left untouched
// (delta-style); use a Light Fade cue to ramp from the current state.
class LightCue : public cues::Cue {
    Q_OBJECT
public:
    explicit LightCue(QObject *parent = nullptr);
    ~LightCue() override;

    QString typeKey()  const override { return QStringLiteral("light"); }
    QString typeName() const override { return tr("Light"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    quint16 universe() const { return m_universe; }
    const QHash<int, int> &channels() const { return m_channels; }

    QVariantMap channelsMap() const;
    void setChannelsMap(const QVariantMap &map);

private:
    quint16         m_universe = 1;
    QHash<int, int> m_channels;
};

// A Light Fade cue ramps a target Light cue's channel set over a
// duration. The target cue's stored values become the destination —
// the fade pulls every channel in that map from its current live value
// toward the target.
class LightFadeCue : public cues::Cue {
    Q_OBJECT
public:
    explicit LightFadeCue(QObject *parent = nullptr);
    ~LightFadeCue() override;

    QString typeKey()  const override { return QStringLiteral("light-fade"); }
    QString typeName() const override { return tr("Light Fade"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    core::CueId targetId()        const { return m_targetId; }
    double      durationSeconds() const { return m_durationSeconds; }

private:
    core::CueId m_targetId;
    double      m_durationSeconds = 3.0;
};

} // namespace quewi::lighting
