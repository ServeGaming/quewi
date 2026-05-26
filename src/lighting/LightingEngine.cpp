#include "lighting/LightingEngine.h"

#include <QTimer>
#include <algorithm>
#include <cstring>

namespace quewi::lighting {

namespace {

constexpr int kTickIntervalMs = 23; // ~44 Hz, matches DMX refresh
int clampChannelValue(int v) { return std::clamp(v, 0, 255); }

} // namespace

LightingEngine::LightingEngine(QObject *parent)
    : QObject(parent)
{
}

LightingEngine::~LightingEngine() = default;

void LightingEngine::ensureRunning()
{
    if (m_running.load()) return;
    if (!m_sender) m_sender = std::make_unique<SacnSender>();
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &LightingEngine::tick);
    }
    m_timer->start(kTickIntervalMs);
    m_running.store(true);
    emit runningChanged(true);
}

void LightingEngine::shutdown()
{
    if (!m_running.load()) return;
    if (m_timer) m_timer->stop();
    m_sender.reset();
    m_universes.clear();
    m_running.store(false);
    emit runningChanged(false);
}

void LightingEngine::setSourceName(const QString &name)
{
    if (m_sender) m_sender->setSourceName(name);
}

void LightingEngine::applyChannels(quint16 universe, const QHash<int, int> &values)
{
    ensureRunning();
    auto &state = m_universes[universe];
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const int ch = it.key();
        if (ch < 1 || ch > 512) continue;
        state.current[ch - 1] = static_cast<quint8>(clampChannelValue(it.value()));
        state.fades.remove(ch);
    }
}

void LightingEngine::fadeChannels(quint16 universe,
                                  const QHash<int, int> &targetValues,
                                  double durationSeconds)
{
    ensureRunning();
    auto &state = m_universes[universe];
    if (durationSeconds <= 0.0) {
        applyChannels(universe, targetValues);
        return;
    }
    for (auto it = targetValues.constBegin(); it != targetValues.constEnd(); ++it) {
        const int ch = it.key();
        if (ch < 1 || ch > 512) continue;
        ChannelFade f;
        f.fromValue       = state.current[ch - 1];
        f.targetValue     = clampChannelValue(it.value());
        f.elapsedSeconds  = 0.0;
        f.durationSeconds = durationSeconds;
        state.fades.insert(ch, f);
    }
}

DmxFrame LightingEngine::snapshotUniverse(quint16 universe) const
{
    auto it = m_universes.constFind(universe);
    if (it == m_universes.constEnd()) return DmxFrame{};
    return it->current;
}

void LightingEngine::blackout()
{
    for (auto it = m_universes.begin(); it != m_universes.end(); ++it) {
        it->current.fill(0);
        it->fades.clear();
    }
}

void LightingEngine::fadeOutAll(double durationSeconds)
{
    if (durationSeconds <= 0.0) { blackout(); return; }
    // Walk every active universe and, for each non-zero channel,
    // queue a fade to 0 over the requested duration. Reuses
    // fadeChannels so superseded-fade semantics match operator
    // expectations (a fade-out cancels any in-flight fade-in on the
    // same channel).
    for (auto it = m_universes.begin(); it != m_universes.end(); ++it) {
        const quint16 universe = it.key();
        const auto &frame = it.value().current;
        QHash<int, int> targets;
        targets.reserve(64);
        for (int ch = 0; ch < 512; ++ch) {
            if (frame[ch] > 0) targets[ch + 1] = 0;   // channels are 1-based
        }
        if (!targets.isEmpty()) {
            fadeChannels(universe, targets, durationSeconds);
        }
    }
}

void LightingEngine::tick()
{
    constexpr double dt = kTickIntervalMs / 1000.0;

    for (auto it = m_universes.begin(); it != m_universes.end(); ++it) {
        const quint16 universe = it.key();
        auto &state = it.value();

        // Advance any active fades, write into current state.
        if (!state.fades.isEmpty()) {
            QList<int> done;
            for (auto fit = state.fades.begin(); fit != state.fades.end(); ++fit) {
                auto &fade = fit.value();
                fade.elapsedSeconds += dt;
                const double t = std::min(1.0, fade.elapsedSeconds / fade.durationSeconds);
                const double v = fade.fromValue + (fade.targetValue - fade.fromValue) * t;
                state.current[fit.key() - 1] = static_cast<quint8>(clampChannelValue(static_cast<int>(v + 0.5)));
                if (t >= 1.0) done.append(fit.key());
            }
            for (int ch : done) state.fades.remove(ch);
        }

        if (m_sender) m_sender->sendUniverse(universe, state.current);
    }
}

} // namespace quewi::lighting
