#pragma once

#include "cues/Cue.h"

#include <QList>

namespace quewi::cues {

// A group cue fires a set of other cues according to one of five modes.
//
// MVP keeps grouping flat: children are still rows in the active cue
// list and the group references them by id. The QLab-style nested
// tree (children visually indented under the group) is a future
// CueList model upgrade — the data model here doesn't preclude it.
class GroupCue : public Cue {
    Q_OBJECT
public:
    enum class Mode {
        Parallel = 0,    // fire all children at once
        Sequential = 1,  // fire children one after another
        StartFirst = 2,  // fire only the first child
        StartRandom = 3, // fire one random child
        Timeline = 4,    // parallel with per-child offsets (uses childOffsets)
    };

    explicit GroupCue(QObject *parent = nullptr);
    ~GroupCue() override;

    QString typeKey()  const override { return QStringLiteral("group"); }
    QString typeName() const override { return tr("Group"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    Mode                  mode()         const { return m_mode; }
    QList<core::CueId>    childIds()     const { return m_childIds; }
    QList<double>         childOffsets() const { return m_childOffsets; }
    double                stepInterval() const { return m_stepInterval; }

private:
    Mode               m_mode = Mode::Sequential;
    QList<core::CueId> m_childIds;
    QList<double>      m_childOffsets;  // seconds, used by Timeline mode
    double             m_stepInterval = 0.0; // pause between sequential children
};

} // namespace quewi::cues
