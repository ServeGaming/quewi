#pragma once

#include <QList>

namespace quewi::audio {

// Hand-rolled VBAP (Vector-Base Amplitude Panning) renderer.
//
// Inputs are a list of speakers with positions on the unit sphere, plus
// a source position. Output is a per-speaker linear gain coefficient
// such that the squared sum is 1.0 (constant-power) and at most three
// speakers receive non-zero gain.
//
// Reference: Pulkki, V. "Virtual sound source positioning using vector
// base amplitude panning." J. Audio Eng. Soc., 1997.
//
// Two operating modes:
//   - 2D / horizontal-only: speakers are projected onto the horizontal
//     plane; source position is azimuth + (ignored) elevation. The
//     renderer picks the adjacent pair surrounding the source azimuth
//     and applies a constant-power crossfade.
//   - 3D / hemisphere with height: speakers form a triangulated sphere;
//     the source is positioned within one triangle and gains are the
//     normalised barycentric weights.
//
// The 3D triangulation is computed once per Speaker layout (cheap — a
// dozen speakers max in practice). Gains are recomputed every time
// the source moves, which is cheap enough to do every audio buffer.

struct Speaker {
    int     channel = 0;     // 0-based output channel
    float   azimuthDeg = 0;  // -180..+180, 0 = front, +90 = right
    float   elevationDeg = 0;// -90..+90, 0 = ear-level
    // Distance is stored but not used by the gain calculation — VBAP
    // assumes equidistant speakers. Kept for the patch dialog.
    float   distance = 1.0f;
};

class Vbap {
public:
    explicit Vbap(const QList<Speaker> &speakers);

    // Returns one gain per channel index up to maxChannels. Channels not
    // mapped to any speaker get 0. The gains are constant-power
    // normalised: sum-of-squares == 1.0 (within float epsilon).
    //
    // azimuthDeg / elevationDeg follow the same convention as Speaker.
    // spread is 0..1 — when 1, the source is omnidirectional and every
    // speaker gets equal gain; when 0 (default) you get pure VBAP.
    QList<float> gains(float azimuthDeg, float elevationDeg,
                       float spread, int maxChannels) const;

    int speakerCount() const { return static_cast<int>(m_speakers.size()); }
    bool hasHeight() const { return m_hasHeight; }

private:
    QList<Speaker> m_speakers;
    bool m_hasHeight = false;

    // Sorted-by-azimuth indices for the 2D pair lookup. Empty in 3D mode.
    QList<int> m_azimSorted;

    // Triangulation for 3D mode — each entry is three indices into
    // m_speakers forming a triangle on the unit sphere. Empty in 2D.
    QList<std::array<int, 3>> m_triangles;

    void buildIndices();
    QList<float> gains2D(float azimuthDeg, int maxChannels) const;
    QList<float> gains3D(float azimuthDeg, float elevationDeg,
                         int maxChannels) const;
};

} // namespace quewi::audio
