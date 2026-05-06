#include "audio/AudioTrajectory.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace quewi::audio {

namespace {

double lerp(double a, double b, double t) { return a + (b - a) * t; }

// Shortest-arc azimuth interpolation. Without this a keyframe at +170°
// followed by one at -170° would sweep through 0° instead of wrapping
// across the rear of the room (340° vs the intended 20°).
double lerpAzimuth(double a, double b, double t)
{
    double diff = b - a;
    while (diff >  180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;
    double v = a + diff * t;
    while (v >  180.0) v -= 360.0;
    while (v < -180.0) v += 360.0;
    return v;
}

} // namespace

void AudioTrajectory::setKeyframes(QList<Keyframe> kfs)
{
    m_keyframes = std::move(kfs);
    sortKeyframes();
}

void AudioTrajectory::addKeyframe(const Keyframe &kf)
{
    m_keyframes.append(kf);
    sortKeyframes();
}

void AudioTrajectory::removeKeyframe(int index)
{
    if (index >= 0 && index < m_keyframes.size())
        m_keyframes.removeAt(index);
}

void AudioTrajectory::updateKeyframe(int index, const Keyframe &kf)
{
    if (index < 0 || index >= m_keyframes.size()) return;
    m_keyframes[index] = kf;
    sortKeyframes();
}

void AudioTrajectory::sortKeyframes()
{
    std::sort(m_keyframes.begin(), m_keyframes.end(),
              [](const Keyframe &a, const Keyframe &b) {
                  return a.timeSeconds < b.timeSeconds;
              });
}

double AudioTrajectory::durationSeconds() const
{
    if (m_keyframes.size() < 2) return 0.0;
    return m_keyframes.last().timeSeconds - m_keyframes.first().timeSeconds;
}

AudioTrajectory::Sample AudioTrajectory::sampleAt(double elapsedSeconds) const
{
    Sample s;
    if (m_keyframes.isEmpty()) return s;
    if (m_keyframes.size() == 1) {
        s.azimuthDeg   = m_keyframes[0].azimuthDeg;
        s.elevationDeg = m_keyframes[0].elevationDeg;
        s.spread       = m_keyframes[0].spread;
        return s;
    }

    const double t0 = m_keyframes.first().timeSeconds;
    const double t1 = m_keyframes.last().timeSeconds;
    const double dur = t1 - t0;

    double t = elapsedSeconds;
    if (m_mode == Mode::Loop && dur > 0.0) {
        t = std::fmod(elapsedSeconds - t0, dur);
        if (t < 0) t += dur;
        t += t0;
    } else {
        // OneShot: clamp to boundaries.
        if (t <= t0) {
            s.azimuthDeg   = m_keyframes.first().azimuthDeg;
            s.elevationDeg = m_keyframes.first().elevationDeg;
            s.spread       = m_keyframes.first().spread;
            return s;
        }
        if (t >= t1) {
            s.azimuthDeg   = m_keyframes.last().azimuthDeg;
            s.elevationDeg = m_keyframes.last().elevationDeg;
            s.spread       = m_keyframes.last().spread;
            return s;
        }
    }

    // Bracket t between two keyframes.
    for (int i = 0; i + 1 < m_keyframes.size(); ++i) {
        const auto &a = m_keyframes[i];
        const auto &b = m_keyframes[i + 1];
        if (t >= a.timeSeconds && t <= b.timeSeconds) {
            const double span = b.timeSeconds - a.timeSeconds;
            const double u = (span > 1e-9) ? (t - a.timeSeconds) / span : 0.0;
            s.azimuthDeg   = lerpAzimuth(a.azimuthDeg, b.azimuthDeg, u);
            s.elevationDeg = lerp(a.elevationDeg, b.elevationDeg, u);
            s.spread       = lerp(a.spread, b.spread, u);
            return s;
        }
    }
    // Fallthrough — shouldn't hit, but degrade to last keyframe.
    s.azimuthDeg   = m_keyframes.last().azimuthDeg;
    s.elevationDeg = m_keyframes.last().elevationDeg;
    s.spread       = m_keyframes.last().spread;
    return s;
}

QJsonObject AudioTrajectory::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("mode"),
             m_mode == Mode::Loop ? QStringLiteral("loop") : QStringLiteral("oneshot"));
    QJsonArray kfs;
    for (const auto &k : m_keyframes) {
        QJsonObject ko;
        ko.insert(QStringLiteral("t"),  k.timeSeconds);
        ko.insert(QStringLiteral("az"), k.azimuthDeg);
        ko.insert(QStringLiteral("el"), k.elevationDeg);
        ko.insert(QStringLiteral("sp"), k.spread);
        kfs.append(ko);
    }
    o.insert(QStringLiteral("keyframes"), kfs);
    return o;
}

AudioTrajectory AudioTrajectory::fromJson(const QJsonObject &o)
{
    AudioTrajectory t;
    t.m_mode = (o.value(QStringLiteral("mode")).toString() == QLatin1String("loop"))
                ? Mode::Loop : Mode::OneShot;
    const auto arr = o.value(QStringLiteral("keyframes")).toArray();
    QList<Keyframe> kfs;
    kfs.reserve(arr.size());
    for (const auto &v : arr) {
        const auto ko = v.toObject();
        Keyframe k;
        k.timeSeconds  = ko.value(QStringLiteral("t")).toDouble();
        k.azimuthDeg   = ko.value(QStringLiteral("az")).toDouble();
        k.elevationDeg = ko.value(QStringLiteral("el")).toDouble();
        k.spread       = ko.value(QStringLiteral("sp")).toDouble();
        kfs.append(k);
    }
    t.setKeyframes(std::move(kfs));
    return t;
}

} // namespace quewi::audio
