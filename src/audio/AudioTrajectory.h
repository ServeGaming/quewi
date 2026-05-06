#pragma once

#include <QJsonObject>
#include <QList>

namespace quewi::audio {

// Animated source position for an object-audio cue. A trajectory is a
// sorted list of keyframes; the renderer linearly interpolates azimuth,
// elevation, and spread between them. v1 keeps it deliberately simple —
// no curves, no per-segment shaping, no spatial blend modes. The "table
// of points" model is what most operators reach for first.
//
// Time origin is the cue's playback position (seconds since the voice
// fired). Times outside the keyframe range either hold the boundary
// value (OneShot) or wrap (Loop).
class AudioTrajectory {
public:
    enum class Mode { OneShot, Loop };

    struct Keyframe {
        double timeSeconds = 0.0;
        double azimuthDeg  = 0.0;
        double elevationDeg= 0.0;
        double spread      = 0.0;   // 0..1
    };

    struct Sample {
        double azimuthDeg  = 0.0;
        double elevationDeg= 0.0;
        double spread      = 0.0;
    };

    bool          isEmpty() const { return m_keyframes.size() < 2; }
    int           keyframeCount() const { return m_keyframes.size(); }
    const QList<Keyframe>& keyframes() const { return m_keyframes; }
    void          setKeyframes(QList<Keyframe> kfs);
    void          addKeyframe(const Keyframe &kf);
    void          removeKeyframe(int index);
    void          updateKeyframe(int index, const Keyframe &kf);

    Mode          mode() const { return m_mode; }
    void          setMode(Mode m) { m_mode = m; }

    // Total duration covered by the keyframes (last - first time). Zero
    // for empty / single-keyframe trajectories.
    double        durationSeconds() const;

    // Sample the trajectory at elapsed cue time. Returns the boundary
    // value if the trajectory is empty / single keyframe.
    Sample        sampleAt(double elapsedSeconds) const;

    QJsonObject   toJson() const;
    static AudioTrajectory fromJson(const QJsonObject &o);

private:
    void sortKeyframes();

    QList<Keyframe> m_keyframes;
    Mode            m_mode = Mode::OneShot;
};

} // namespace quewi::audio
