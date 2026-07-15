#pragma once

// Deliberately depends on nothing but Qt. MixShow is compiled into quewi_core
// (see src/core/CMakeLists.txt) so Workspace can own one — exactly as cues/ is,
// and for the same reason: linking core against quewi_mix would be circular.
// Keep it that way. No ConsoleLink, no protocol types in here.

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace quewi::mix {

// One controlled channel — in practice, one radio mic.
struct MixChannel {
    int     strip = 0;      // console strip number, 1-based, as printed
    QString name;           // what the operator calls it: "Elphaba"
    QString actor;          // who's wearing it tonight; see MixShow::actors
    int     backupStrip = 0;// 0 = none. The spare to switch to when this dies.

    bool isValid() const { return strip >= 1; }
};

// A named group of channels assigned as one unit — "Ensemble Women",
// "Orchestra". Saves putting 20 channels on a DCA by hand for every cue.
using Ensemble = QSet<int>;   // strip numbers

// The non-per-cue half of the mix show: what channels exist, who's on them,
// and what groups they form. Cues reference these by number/name and stay thin.
//
// Lives on the Workspace alongside CueLists, PatchManager and CartGrid, and
// serialises into the show file so it travels with the show.
class MixShow : public QObject {
    Q_OBJECT
public:
    explicit MixShow(QObject *parent = nullptr);
    ~MixShow() override;

    // ── Channels ─────────────────────────────────────────────────────
    QVector<MixChannel> channels() const;      // ordered by strip
    MixChannel channel(int strip) const;       // invalid if absent
    bool       hasChannel(int strip) const;

    void setChannel(const MixChannel &channel);   // insert or replace
    void removeChannel(int strip);

    // ── Ensembles ────────────────────────────────────────────────────
    QStringList ensembleNames() const;          // sorted
    Ensemble    ensemble(const QString &name) const;
    void        setEnsemble(const QString &name, const Ensemble &strips);
    void        removeEnsemble(const QString &name);

    // Expand a mixed list of strip numbers and ensemble names into plain
    // strips. Unknown names and strips with no channel are dropped, so a cue
    // referencing a deleted ensemble degrades to "fewer mics open" rather than
    // to a crash or a stuck-open mic.
    QSet<int> resolve(const QSet<int> &strips, const QStringList &ensembles) const;

    // ── Recasting ────────────────────────────────────────────────────
    //
    // Swap every reference to one strip for another across the whole show.
    // Returns the number of channels touched. Cue-level reassignment is a
    // separate operation (see MixCue) — this only moves the channel identity.
    int reassignStrip(int fromStrip, int toStrip);

    // ── Persistence ──────────────────────────────────────────────────
    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &json);
    void        clear();

    bool isEmpty() const { return m_channels.isEmpty() && m_ensembles.isEmpty(); }

signals:
    void channelsChanged();
    void ensemblesChanged();

private:
    QHash<int, MixChannel>   m_channels;    // strip -> channel
    QHash<QString, Ensemble> m_ensembles;
};

} // namespace quewi::mix
