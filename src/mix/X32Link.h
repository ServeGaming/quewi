#pragma once

#include "mix/ConsoleLink.h"
#include "mix/X32Value.h"
#include "osc/OscMessage.h"

#include <QHostAddress>
#include <memory>

class QUdpSocket;
class QTimer;

namespace quewi::mix {

// Behringer X32 / Midas M32 console link. OSC over UDP 10023.
// Protocol details and sourcing: docs/dev/console-protocols.md.
class X32Link : public ConsoleLink {
    Q_OBJECT
public:
    explicit X32Link(QObject *parent = nullptr);
    ~X32Link() override;

    void    connectToConsole(const QString &host, quint16 port = 0) override;
    void    disconnectFromConsole() override;
    quint16 defaultPort() const override { return 10023; }
    QString protocolName() const override { return QStringLiteral("X32/M32"); }

    void setDcaLabel(int dca, const QString &name) override;
    void setChannelMuted(int channel, bool muted) override;

    // ── Scene Safe ───────────────────────────────────────────────────
    //
    // The X32's Scene Safe bitmap has bit 5 = "Groups (DCA assign, Mute group
    // assign)". If it isn't set, ANY scene recall — by the operator, or by us —
    // silently reverts every assignment we've made. Mid-show. No error.
    //
    // We check on connect and report it. Running a show with this unsafed is
    // not a degraded mode, it's a broken one, so the UI must refuse rather
    // than warn quietly.
    bool sceneSafeGroupsEnabled() const { return m_groupsSafed; }

    // Ask the console to set bit 5 for us. The alternative is talking an
    // operator through a menu tree during a tech.
    void requestSceneSafeGroups();

    // ── Channel links ────────────────────────────────────────────────
    //
    // If a channel pair is linked, writing one channel's mute moves its
    // partner too. Read at connect; without it we generate moves the operator
    // didn't ask for and can't explain.
    bool isChannelLinked(int channel) const;

signals:
    void sceneSafeGroupsChanged(bool safed);

    // Emitted when /xremote registration looks like it failed. The console
    // accepts only FOUR clients total, competing with X32-Edit, tablets and
    // any Companion instance. Silently not receiving console changes is the
    // worst possible failure for live capture, so this is loud.
    void remoteRegistrationLost();

protected:
    void writeDcaAssignment(int channel, const DcaSet &previous,
                            const DcaSet &next, bool previousKnown) override;

private slots:
    void onRxReadyRead();
    void onKeepaliveTick();

private:
    // The console replies to the SOURCE PORT of each datagram, and it does not
    // echo changes back to whoever made them. So we run two sockets:
    //
    //   rx  — registers /xremote and receives everything, including replies
    //         to queries we send from it.
    //   tx  — sends every Set. The console sees a different source port,
    //         considers it "another client", and therefore relays the change
    //         to rx. That echo is our only confirmation that a UDP set landed.
    //
    // Without this, a dropped mute is undetectable. Companion does the same
    // thing in production for the same reason.
    void sendFrom(QUdpSocket *sock, const osc::Message &m);
    void query(const osc::Message &m) { sendFrom(m_rx.get(), m); }
    void set(const osc::Message &m)   { sendFrom(m_tx.get(), m); }

    void handleMessage(const osc::Message &m);
    void handleInfoReply(const osc::Message &m);
    void requestInitialState();

    std::unique_ptr<QUdpSocket> m_rx;
    std::unique_ptr<QUdpSocket> m_tx;
    std::unique_ptr<QTimer>     m_keepalive;

    QHostAddress m_host;
    quint16      m_port = 0;

    bool     m_groupsSafed = false;
    quint8   m_sceneSafeInputs = 0;
    QSet<int> m_linkedChannels;   // 1-based; both members of a linked pair

    // Liveness. Every keepalive also sends /info, which the desk always
    // answers; if several windows pass with NO reply at all, the console is
    // gone (cable, power, IP change) and we go to Failed — quewi used to stay
    // "Connected" forever, firing DCA cues into nothing. Any reply afterwards
    // recovers automatically with a full resync.
    int  m_silentTicks = 0;
    bool m_lostContact = false;

    // Registration. The console relays our tx socket's sets to rx only while
    // we hold one of its four /xremote slots. When a DCA change we KNOW is a
    // change isn't relayed back within two windows, the slot is gone. (This
    // used to be "no traffic for 3 s", which fired on any untouched desk —
    // /xremote itself is never answered.)
    QSet<int> m_echoPending;   // channels whose DCA write we're awaiting
    int       m_echoTicks = 0;

    // Initial-sync race. On connect / resync we ask for every channel's DCA
    // mask; if we WRITE a channel before its reply lands, the late reply
    // carries the desk's pre-write value and used to overwrite the cache —
    // so a later cue wanting that stale value was skipped as a "no-op" and
    // the mic stayed on the wrong DCA. While a channel's sync reply is
    // outstanding we remember what we wrote, and a differing reply is
    // recognised as stale and dropped.
    QSet<int>      m_syncPending;
    QHash<int,int> m_writtenMask;
};

} // namespace quewi::mix
