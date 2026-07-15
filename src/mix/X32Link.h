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
                            const DcaSet &next) override;

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

    // Set when we send /xremote, cleared when the console says anything.
    // Two consecutive silent keepalive windows means we are almost certainly
    // not one of the four registered clients.
    int m_silentKeepalives = 0;
};

} // namespace quewi::mix
