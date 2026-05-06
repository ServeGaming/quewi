#include "audio/Vbap.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

namespace quewi::audio {

namespace {

constexpr float kHeightThresholdDeg = 5.0f;
constexpr float kPi = 3.14159265358979323846f;

inline float deg2rad(float d) { return d * (kPi / 180.0f); }

struct Vec3 { float x, y, z; };

Vec3 sphericalToCartesian(float azDeg, float elDeg)
{
    const float az = deg2rad(azDeg);
    const float el = deg2rad(elDeg);
    const float c  = std::cos(el);
    return { std::sin(az) * c, std::cos(az) * c, std::sin(el) };
}

float dot(const Vec3 &a, const Vec3 &b)
{ return a.x*b.x + a.y*b.y + a.z*b.z; }

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

float norm(const Vec3 &v) { return std::sqrt(dot(v, v)); }

// Normalise an azimuth value into [-180, +180].
float wrapAzimuth(float az)
{
    while (az >  180.0f) az -= 360.0f;
    while (az < -180.0f) az += 360.0f;
    return az;
}

// Compute barycentric coordinates of a point against a spherical
// triangle. Returns true if the point lies inside; weights sum to 1
// when interior. We use the planar barycentric formula on the
// unit-sphere intersection — for adjacent speakers and reasonable
// triangle sizes this is accurate enough.
bool barycentric(const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c,
                 float &wa, float &wb, float &wc)
{
    const Vec3 v0 { b.x-a.x, b.y-a.y, b.z-a.z };
    const Vec3 v1 { c.x-a.x, c.y-a.y, c.z-a.z };
    const Vec3 v2 { p.x-a.x, p.y-a.y, p.z-a.z };

    const float d00 = dot(v0, v0);
    const float d01 = dot(v0, v1);
    const float d11 = dot(v1, v1);
    const float d20 = dot(v2, v0);
    const float d21 = dot(v2, v1);

    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-9f) return false;

    wb = (d11 * d20 - d01 * d21) / denom;
    wc = (d00 * d21 - d01 * d20) / denom;
    wa = 1.0f - wb - wc;

    constexpr float eps = 1e-4f;
    return wa >= -eps && wb >= -eps && wc >= -eps;
}

} // namespace

Vbap::Vbap(const QList<Speaker> &speakers)
    : m_speakers(speakers)
{
    for (const auto &s : speakers)
        if (std::abs(s.elevationDeg) > kHeightThresholdDeg) { m_hasHeight = true; break; }
    buildIndices();
}

void Vbap::buildIndices()
{
    if (!m_hasHeight) {
        // 2D mode — sort speakers by azimuth.
        m_azimSorted.clear();
        m_azimSorted.reserve(m_speakers.size());
        for (int i = 0; i < m_speakers.size(); ++i) m_azimSorted.append(i);
        std::sort(m_azimSorted.begin(), m_azimSorted.end(),
                  [this](int a, int b) {
                      return m_speakers[a].azimuthDeg < m_speakers[b].azimuthDeg;
                  });
        return;
    }

    // 3D mode — build a simple fan triangulation. Group speakers by
    // elevation tier (low / ear-level / high) and emit triangles between
    // adjacent tiers + horizontal pairs. This is not a Delaunay
    // triangulation but it works for the small symmetric layouts we
    // ship (Atmos 7.1.4 = 7 ear + 4 height) and lets the user supply
    // custom rigs that we still cover.
    QList<int> ear, high;
    for (int i = 0; i < m_speakers.size(); ++i) {
        if (m_speakers[i].elevationDeg > kHeightThresholdDeg) high.append(i);
        else ear.append(i);
    }
    auto sortByAz = [this](QList<int> &v) {
        std::sort(v.begin(), v.end(), [this](int a, int b) {
            return m_speakers[a].azimuthDeg < m_speakers[b].azimuthDeg;
        });
    };
    sortByAz(ear); sortByAz(high);

    // Adjacent ear-level pairs become "horizontal" triangles closed by
    // the nearest height speaker.
    for (int i = 0; i < ear.size(); ++i) {
        const int e0 = ear[i];
        const int e1 = ear[(i + 1) % ear.size()];
        // Find the height speaker whose azimuth is closest to the
        // midpoint of (e0, e1).
        const float midAz = wrapAzimuth(
            (m_speakers[e0].azimuthDeg + m_speakers[e1].azimuthDeg) * 0.5f);
        int bestH = -1;
        float bestD = 360.0f;
        for (int h : high) {
            const float d = std::abs(wrapAzimuth(m_speakers[h].azimuthDeg - midAz));
            if (d < bestD) { bestD = d; bestH = h; }
        }
        if (bestH >= 0) {
            m_triangles.append({ e0, e1, bestH });
        }
    }

    // If there is more than one height speaker, also emit triangles
    // among them so an overhead source uses height-only weighting.
    if (high.size() >= 2) {
        for (int i = 0; i < high.size(); ++i) {
            const int h0 = high[i];
            const int h1 = high[(i + 1) % high.size()];
            // Pair with the nearest ear speaker for closure.
            const float midAz = wrapAzimuth(
                (m_speakers[h0].azimuthDeg + m_speakers[h1].azimuthDeg) * 0.5f);
            int bestE = -1;
            float bestD = 360.0f;
            for (int e : ear) {
                const float d = std::abs(wrapAzimuth(m_speakers[e].azimuthDeg - midAz));
                if (d < bestD) { bestD = d; bestE = e; }
            }
            if (bestE >= 0) {
                m_triangles.append({ h0, h1, bestE });
            }
        }
    }
}

QList<float> Vbap::gains(float azimuthDeg, float elevationDeg,
                         float spread, int maxChannels) const
{
    QList<float> out;
    out.fill(0.0f, maxChannels);
    if (m_speakers.isEmpty()) return out;

    QList<float> base = m_hasHeight
        ? gains3D(azimuthDeg, elevationDeg, maxChannels)
        : gains2D(azimuthDeg, maxChannels);

    if (spread <= 0.0f) return base;

    // Spread mixes the base VBAP gains toward an equal-power omni
    // distribution. At spread=1, every speaker gets the same gain.
    const float s = std::clamp(spread, 0.0f, 1.0f);
    const float omni = std::sqrt(1.0f / float(m_speakers.size()));
    for (auto &spk : m_speakers) {
        const int ch = spk.channel;
        if (ch < 0 || ch >= maxChannels) continue;
        const float a = base[ch];
        // Linear interpolation between VBAP and omni, then re-normalise
        // to constant power.
        base[ch] = (1.0f - s) * a + s * omni;
    }
    // Renormalise.
    float sumSq = 0.0f;
    for (float g : base) sumSq += g * g;
    if (sumSq > 1e-9f) {
        const float k = 1.0f / std::sqrt(sumSq);
        for (auto &g : base) g *= k;
    }
    return base;
}

QList<float> Vbap::gains2D(float azimuthDeg, int maxChannels) const
{
    QList<float> out;
    out.fill(0.0f, maxChannels);
    if (m_azimSorted.isEmpty()) return out;

    const float az = wrapAzimuth(azimuthDeg);

    // Find the pair of speakers whose azimuths bracket the source.
    const int n = m_azimSorted.size();
    int lo = -1;
    for (int i = 0; i < n; ++i) {
        const float a0 = m_speakers[m_azimSorted[i]].azimuthDeg;
        const float a1 = m_speakers[m_azimSorted[(i + 1) % n]].azimuthDeg;
        // Wrap-aware: the pair (a0, a1) covers the arc from a0 to a1
        // going through the "next" direction. Inside-arc test:
        const float span = wrapAzimuth(a1 - a0);
        const float t    = wrapAzimuth(az - a0);
        if (span > 0 ? (t >= 0 && t <= span)
                     : (t <= 0 && t >= span)) { lo = i; break; }
    }
    if (lo < 0) lo = 0;

    const int hi  = (lo + 1) % n;
    const int s0  = m_azimSorted[lo];
    const int s1  = m_azimSorted[hi];

    const float a0   = m_speakers[s0].azimuthDeg;
    const float a1   = m_speakers[s1].azimuthDeg;
    const float span = std::max(1e-6f, std::abs(wrapAzimuth(a1 - a0)));
    const float frac = std::clamp(std::abs(wrapAzimuth(az - a0)) / span, 0.0f, 1.0f);

    // Constant-power crossfade.
    const float g0 = std::cos(frac * (kPi * 0.5f));
    const float g1 = std::sin(frac * (kPi * 0.5f));

    const int c0 = m_speakers[s0].channel;
    const int c1 = m_speakers[s1].channel;
    if (c0 >= 0 && c0 < maxChannels) out[c0] = g0;
    if (c1 >= 0 && c1 < maxChannels) out[c1] = g1;
    return out;
}

QList<float> Vbap::gains3D(float azimuthDeg, float elevationDeg,
                           int maxChannels) const
{
    QList<float> out;
    out.fill(0.0f, maxChannels);
    if (m_triangles.isEmpty()) return gains2D(azimuthDeg, maxChannels);

    const Vec3 p = sphericalToCartesian(azimuthDeg, elevationDeg);

    // Find the triangle whose barycentric coords for p are all >= 0.
    // Among multiple candidates, prefer the one with the smallest
    // negative coordinate magnitude (= source closest to the triangle).
    int bestTri = -1;
    float bestSlack = -1e9f;
    std::array<float, 3> bestW = {0, 0, 0};
    for (int t = 0; t < m_triangles.size(); ++t) {
        const auto &tri = m_triangles[t];
        const Vec3 a = sphericalToCartesian(m_speakers[tri[0]].azimuthDeg,
                                             m_speakers[tri[0]].elevationDeg);
        const Vec3 b = sphericalToCartesian(m_speakers[tri[1]].azimuthDeg,
                                             m_speakers[tri[1]].elevationDeg);
        const Vec3 c = sphericalToCartesian(m_speakers[tri[2]].azimuthDeg,
                                             m_speakers[tri[2]].elevationDeg);
        float wa, wb, wc;
        if (barycentric(p, a, b, c, wa, wb, wc)) {
            bestTri = t;
            bestW = { wa, wb, wc };
            break;
        }
        // Track the least-negative for fallback.
        const float slack = std::min({ wa, wb, wc });
        if (slack > bestSlack) {
            bestSlack = slack;
            bestTri = t;
            bestW = { std::max(0.0f, wa), std::max(0.0f, wb), std::max(0.0f, wc) };
        }
    }
    if (bestTri < 0) return out;

    // Normalise weights to constant power.
    float sumSq = 0.0f;
    for (float w : bestW) sumSq += w * w;
    if (sumSq < 1e-12f) return out;
    const float k = 1.0f / std::sqrt(sumSq);

    const auto &tri = m_triangles[bestTri];
    for (int i = 0; i < 3; ++i) {
        const int ch = m_speakers[tri[i]].channel;
        if (ch < 0 || ch >= maxChannels) continue;
        out[ch] = bestW[i] * k;
    }
    return out;
}

} // namespace quewi::audio
