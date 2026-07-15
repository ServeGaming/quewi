#pragma once

#include "cues/Cue.h"
#include "mix/ConsoleLink.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace quewi::mix {

class MixShow;

// One mix cue: which mics are on which DCAs for this scene.
//
// Stored DCA-first — "DCA 3 is Elphaba" — because that's the operator's mental
// model and what the cue grid shows. The channel-first view the console link
// needs is derived (see channelAssignments), not stored, so the two can't
// drift out of sync.
//
// Lives in a CueList of Kind::Mix, so it inherits numbering, names, notes,
// colour and the undo stack from Cue for free.
class MixCue : public cues::Cue {
    Q_OBJECT
public:
    explicit MixCue(QObject *parent = nullptr);
    ~MixCue() override;

    QString typeKey() const override  { return QStringLiteral("mix"); }
    QString typeName() const override { return tr("Mix"); }

    // ── Assignments ──────────────────────────────────────────────────
    //
    // A DCA holds any mix of explicit strips and named ensembles. Ensembles
    // stay unresolved in the cue so that editing "Ensemble Women" updates
    // every cue that uses it, rather than baking today's membership into 200
    // cues.
    QSet<int>   dcaStrips(int dca) const;
    QStringList dcaEnsembles(int dca) const;
    void setDcaStrips(int dca, const QSet<int> &strips);
    void setDcaEnsembles(int dca, const QStringList &ensembles);
    void clearDca(int dca);

    QList<int> assignedDcas() const;      // sorted; only non-empty DCAs
    bool       isEmpty() const;

    // ── Derived view for the console ─────────────────────────────────
    //
    // Inverts DCA->channels into channel->DCAs, resolving ensembles against
    // `show`. This is what ConsoleLink::applyCue consumes.
    //
    // A channel appearing on two DCAs is legal and lands on both — the console
    // supports it (the mask/boolean are per-pair), and it's occasionally what
    // you want for a character who's also in the ensemble number.
    QHash<int, DcaSet> channelAssignments(const MixShow &show) const;

    // ── Recasting ────────────────────────────────────────────────────
    // Swap one strip for another wherever it appears in this cue.
    // Returns true if anything changed.
    bool reassignStrip(int fromStrip, int toStrip);

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

private:
    struct DcaEntry {
        QSet<int>   strips;
        QStringList ensembles;
        bool isEmpty() const { return strips.isEmpty() && ensembles.isEmpty(); }
    };
    QHash<int, DcaEntry> m_dcas;      // 1-based DCA -> what's on it
};

} // namespace quewi::mix
