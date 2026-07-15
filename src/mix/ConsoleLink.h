#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

namespace quewi::mix {

// A DCA membership set, in the caller's language: "this channel belongs to
// DCAs 1 and 3". 1-based, because that's what's printed on the console.
using DcaSet = QSet<int>;

// Abstract link to a mixing console.
//
// The two protocols we target disagree on nearly everything — UDP/binary vs
// TCP/ASCII, an 8-bit bitmask vs a per-(channel,DCA) boolean, 8 DCAs vs 24,
// self-echo vs none. This interface deliberately speaks the CALLER's language
// and lets each link decide how to put it on the wire. See
// docs/dev/console-protocols.md.
//
// The base owns the assignment cache, and that is not an implementation
// detail — it's the point. Both protocols need it, for opposite reasons:
//
//   X32 has no per-membership address. grp/dca is replace-not-toggle, so
//   assigning one DCA requires the WHOLE mask, which requires knowing the
//   current one. Read-modify-write against cache.
//
//   DM7 has only per-membership addresses. A full sync is 120x24 = 2880
//   messages, so writing every pair on every cue is not viable. It needs the
//   DIFF.
//
// Caching centrally and handing subclasses (previous, next) serves both: X32
// ignores `previous` and writes the mask; DM7 writes only the difference.
//
// THREADING: GUI thread only. Sockets are Qt-signal driven; there is no audio
// thread here and nothing below should acquire a lock.
class ConsoleLink : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Failed };
    Q_ENUM(State)

    // What the console in front of us can actually do. Populated on connect.
    //
    // Every field here varies between our two targets and, on Yamaha, between
    // firmware versions of the SAME model. Do not hardcode any of it: the
    // published parameter tables proved to be curated snapshots that
    // under-report, which is exactly how a previous conclusion went wrong.
    // Ask the desk.
    struct Capabilities {
        QString model;            // "X32C", "DM7", ...
        QString firmware;         // DM7: devinfo version. X32: /info arg[3].
        int  channelCount   = 0;  // X32 always 32; DM7 120, Compact 72
        int  dcaCount       = 0;  // X32 8; DM7 24
        int  muteGroupCount = 0;  // X32 6; DM7 12
        bool inputMetering  = false;  // confirmed on both targets
        bool channelEq      = false;  // X32 yes; DM7 yes; CL/QL apparently not
        bool liveCapture    = false;  // does the console push surface changes?
    };

    explicit ConsoleLink(QObject *parent = nullptr);
    ~ConsoleLink() override;

    // ── Connection ───────────────────────────────────────────────────
    virtual void connectToConsole(const QString &host, quint16 port = 0) = 0;
    virtual void disconnectFromConsole() = 0;

    // The default port for this protocol (X32 10023, DM7 49280), so callers
    // and the patch editor don't have to know.
    virtual quint16 defaultPort() const = 0;
    virtual QString protocolName() const = 0;

    State state() const { return m_state; }
    const Capabilities &capabilities() const { return m_caps; }
    QString lastError() const { return m_lastError; }

    // ── DCA assignment ───────────────────────────────────────────────
    //
    // Channels and DCAs are 1-BASED throughout this interface — matching what
    // the operator sees printed on the desk. Each link converts to whatever
    // its protocol wants (both happen to be 0-based on the wire, which is
    // precisely why the conversion belongs in one place).
    //
    // Out-of-range values are dropped rather than sent, so a bad cue can't
    // corrupt console state.
    void   setDcaAssignment(int channel, const DcaSet &dcas);
    DcaSet dcaAssignment(int channel) const;

    // Apply a whole cue at once. Channels absent from `assignments` are muted,
    // which is the TheatreMix rule and the reason the workflow is safe: if a
    // mic isn't in this scene, it is off, with no way to forget.
    void applyCue(const QHash<int, DcaSet> &assignments);

    virtual void setDcaLabel(int dca, const QString &name) = 0;
    virtual void setChannelMuted(int channel, bool muted) = 0;

    // We deliberately do NOT expose setDcaFader(). See the spec: quewi Mix
    // never recalls DCA fader levels. The operator owns the mix. Adding a
    // setter here is how that principle would erode.

signals:
    void stateChanged(quewi::mix::ConsoleLink::State state);
    void capabilitiesChanged();

    // Console-originated changes only — never an echo of our own writes.
    // This is what live-edit capture consumes, so a link that can't
    // distinguish its own echo MUST NOT emit these (see X32Link's
    // two-socket arrangement and DM7's OK-vs-NOTIFY status field).
    void surfaceDcaAssignmentChanged(int channel, quewi::mix::DcaSet dcas);
    void surfaceChannelMuteChanged(int channel, bool muted);

    // Non-fatal; the link stays up. Fatal problems go through stateChanged.
    void errorOccurred(const QString &message);

    // The console did something that invalidates our whole cached view — a
    // scene recall, typically. Both protocols have this problem: neither
    // enumerates what a recall changed, so the only correct response is a
    // full resync.
    void resyncRequired(const QString &reason);

protected:
    void setState(State s);
    void setCapabilities(const Capabilities &caps);
    void setError(const QString &message);   // emits errorOccurred

    // Push a console-originated assignment into the cache and notify. Links
    // call this from their change-notification path. Does not write back.
    void noteSurfaceDcaAssignment(int channel, const DcaSet &dcas);

    // Write an assignment change to the wire.
    //
    // `previous` is what we believed before, `next` is what's wanted. X32
    // ignores `previous` and writes the full mask; DM7 writes only the pairs
    // that differ. Both are correct implementations of the same call.
    virtual void writeDcaAssignment(int channel, const DcaSet &previous,
                                    const DcaSet &next) = 0;

    bool isChannelValid(int channel) const;
    bool isDcaValid(int dca) const;
    // Drops out-of-range DCAs. Used before anything reaches the wire.
    DcaSet sanitize(const DcaSet &dcas) const;

    // Cache of what we believe the console holds, keyed by 1-based channel.
    QHash<int, DcaSet> m_dcaCache;

private:
    State        m_state = State::Disconnected;
    Capabilities m_caps;
    QString      m_lastError;
};

} // namespace quewi::mix
