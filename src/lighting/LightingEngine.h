#pragma once

#include "lighting/Sacn.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <atomic>
#include <memory>

class QTimer;

namespace quewi::lighting {

// Owns the active universe states and the sACN sender. Ticks at 44 Hz
// (the standard DMX refresh rate) and broadcasts every active universe.
//
// Light cues mutate state via setChannel() and applyChannels(). Light
// fade cues drive the fader animation each tick.
//
// Phase 4 ships with sACN multicast only. Art-Net and DMX-USB land as
// follow-up commits — they slot in next to the SacnSender as additional
// outputs gated by the patch (TBD: a full patch model lands when other
// subsystems also need shared destinations).
class LightingEngine : public QObject {
    Q_OBJECT
public:
    explicit LightingEngine(QObject *parent = nullptr);
    ~LightingEngine() override;

    // Set the channels listed in `values` (channel 1..512 → byte 0..255)
    // on the given universe. Channels not in the map are left alone.
    void applyChannels(quint16 universe, const QHash<int, int> &values);

    // Drive a fade from the current values toward `targetValues` over
    // `durationSeconds`. Existing fades on the same channels are
    // superseded. A fade with duration <= 0 is applied immediately.
    void fadeChannels(quint16 universe,
                      const QHash<int, int> &targetValues,
                      double durationSeconds);

    // Snapshot the current universe state — useful for "current state" cues.
    DmxFrame snapshotUniverse(quint16 universe) const;

    // Black out every active universe immediately (panic).
    void blackout();

    // Soft blackout — fades every non-zero channel on every active
    // universe down to 0 over `durationSeconds`. Uses fadeChannels
    // internally per universe. Duration ≤ 0 falls back to instant
    // blackout. Used by /quewi/lighting/fadeOut and /quewi/fadeAll.
    void fadeOutAll(double durationSeconds);

    // True between ensureRunning() and shutdown(). Idle means no socket
    // and no tick — no traffic on the wire when quewi has no lighting.
    bool isRunning() const { return m_running.load(); }
    void ensureRunning();
    void shutdown();

    void setSourceName(const QString &name);

signals:
    void runningChanged(bool running);

private slots:
    void tick();

private:
    struct ChannelFade {
        int    fromValue = 0;
        int    targetValue = 0;
        double elapsedSeconds = 0.0;
        double durationSeconds = 0.0;
    };
    struct UniverseState {
        DmxFrame                    current{};
        QHash<int, ChannelFade>     fades;     // 1-based channel
    };

    QHash<quint16, UniverseState> m_universes;
    std::unique_ptr<SacnSender>   m_sender;
    QTimer                       *m_timer = nullptr;
    std::atomic<bool>             m_running{false};
};

} // namespace quewi::lighting
