#pragma once

#include "osc/OscEngine.h"

#include <QPointer>
#include <QWidget>

class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QLabel;

namespace quewi::ui {

// Live monitor for OSC traffic. Listens to OscEngine::packetSeen and
// renders both directions in a table; clicking a row reveals a detail
// pane with hex bytes plus the decoded address + arg list.
//
// Also exposes inbound listener controls (UDP/TCP/WebSocket port +
// start/stop) so the user can configure them without leaving the window.
class OscMonitor : public QWidget {
    Q_OBJECT
public:
    explicit OscMonitor(osc::OscEngine *engine, QWidget *parent = nullptr);
    ~OscMonitor() override;

private slots:
    void onPacketSeen(const quewi::osc::PacketEvent &event);
    void onRowSelected();
    void onClearClicked();
    void onPauseToggled(bool paused);
    void onUdpToggle(bool checked);
    void onTcpToggle(bool checked);
    void onWsToggle(bool checked);

private:
    void appendDetailFor(const quewi::osc::PacketEvent &event);

    osc::OscEngine *m_engine;

    QTableWidget   *m_table     = nullptr;
    QPlainTextEdit *m_detail    = nullptr;
    QPushButton    *m_clearBtn  = nullptr;
    QPushButton    *m_pauseBtn  = nullptr;

    QSpinBox       *m_udpPort   = nullptr;
    QPushButton    *m_udpToggle = nullptr;
    QSpinBox       *m_tcpPort   = nullptr;
    QPushButton    *m_tcpToggle = nullptr;
    QSpinBox       *m_wsPort    = nullptr;
    QPushButton    *m_wsToggle  = nullptr;
    QLabel         *m_statusLabel = nullptr;

    bool m_paused = false;
    bool m_udpListening = false;
    bool m_tcpListening = false;
    bool m_wsListening = false;

    // Stored events (so a detail pane can decode after the fact).
    std::vector<osc::PacketEvent> m_events;
};

} // namespace quewi::ui
