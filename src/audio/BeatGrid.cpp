#include "audio/BeatGrid.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace quewi::audio {

// ── BeatGrid ─────────────────────────────────────────────────────────────────

double BeatGrid::beatAt(double seconds) const
{
    return isSet() ? (seconds - firstBeat) / beatLength() : 0.0;
}

bool BeatGrid::isBarLine(long long beat) const
{
    const int bpb = std::max(1, beatsPerBar);
    return ((beat % bpb) + bpb) % bpb == 0;
}

double BeatGrid::snap(double seconds, int every) const
{
    if (!isSet()) return seconds;
    const int n = std::max(1, every);
    const double b = std::round(beatAt(seconds) / n) * n;
    return std::max(0.0, timeOfBeat(b));
}

std::vector<double> BeatGrid::beatsIn(double from, double to, int every) const
{
    std::vector<double> out;
    if (!isSet() || to <= from) return out;
    const int n = std::max(1, every);
    // First multiple of n at or after `from` (tiny tolerance so a range that
    // starts exactly on a beat includes it).
    long long b = static_cast<long long>(std::ceil(beatAt(from) / n - 1e-6)) * n;
    for (;; b += n) {
        const double t = timeOfBeat(double(b));
        if (t >= to - 1e-9) break;
        if (t >= 0.0) out.push_back(t);
        if (out.size() > 100000) break;   // a typo'd 99999 BPM can't hang us
    }
    return out;
}

QJsonObject BeatGrid::toJson() const
{
    return QJsonObject{
        {QStringLiteral("bpm"),         bpm},
        {QStringLiteral("firstBeat"),   firstBeat},
        {QStringLiteral("beatsPerBar"), beatsPerBar},
    };
}

BeatGrid BeatGrid::fromJson(const QJsonObject &o)
{
    BeatGrid g;
    g.bpm         = std::clamp(o.value(QStringLiteral("bpm")).toDouble(0.0), 0.0, 999.0);
    g.firstBeat   = std::max(0.0, o.value(QStringLiteral("firstBeat")).toDouble(0.0));
    g.beatsPerBar = std::clamp(o.value(QStringLiteral("beatsPerBar")).toInt(4), 1, 32);
    return g;
}

LightTriggers fillWithBeats(const BeatGrid &grid, double from, double to, int every,
                            const TriggerAction &action, const QString &namePrefix)
{
    LightTriggers out;
    int i = 1;
    for (const double t : grid.beatsIn(from, to, every)) {
        LightTrigger x;
        x.start = t;
        x.name = QStringLiteral("%1 %2").arg(namePrefix).arg(i++);
        x.enter = action;
        out.push_back(std::move(x));
    }
    return out;
}

// ── Tap tempo ────────────────────────────────────────────────────────────────

double TapTempo::tap(double seconds)
{
    if (!m_taps.empty() && (seconds - m_taps.back() > 2.0 || seconds <= m_taps.back()))
        reset();
    m_taps.push_back(seconds);
    if (m_taps.size() > 9) m_taps.erase(m_taps.begin());   // the last 8 intervals
    if (m_taps.size() < 2) return m_bpm = 0.0;
    std::vector<double> gaps;
    for (size_t i = 1; i < m_taps.size(); ++i) gaps.push_back(m_taps[i] - m_taps[i - 1]);
    // Median: one fumbled tap doesn't drag the tempo.
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    const double g = gaps[gaps.size() / 2];
    return m_bpm = g > 0.0 ? 60.0 / g : 0.0;
}

// ── Tempo detection ──────────────────────────────────────────────────────────

namespace {

constexpr double kHopsPerSecond = 100.0;   // 10 ms analysis hops

double at(const std::vector<double> &v, double i)   // linear interpolation
{
    if (i < 0.0) return 0.0;
    const auto i0 = static_cast<size_t>(i);
    if (i0 + 1 >= v.size()) return i0 < v.size() ? v[i0] : 0.0;
    const double f = i - double(i0);
    return v[i0] * (1.0 - f) + v[i0 + 1] * f;
}

} // namespace

TempoEstimate estimateTempo(const float *mono, std::size_t frames, int sampleRate)
{
    TempoEstimate est;
    if (!mono || sampleRate <= 0) return est;
    const auto hop = static_cast<std::size_t>(sampleRate / kHopsPerSecond);
    if (hop == 0 || frames < hop * 400) return est;   // under ~4 s: not enough to tell

    // Onset strength: the rise in log energy from one hop to the next.
    const std::size_t hops = frames / hop;
    std::vector<double> energy(hops);
    for (std::size_t h = 0; h < hops; ++h) {
        double e = 0.0;
        const float *p = mono + h * hop;
        for (std::size_t i = 0; i < hop; ++i) e += double(p[i]) * p[i];
        energy[h] = std::log(1e-10 + e / double(hop));
    }
    std::vector<double> onset(hops, 0.0);
    for (std::size_t h = 1; h < hops; ++h) onset[h] = std::max(0.0, energy[h] - energy[h - 1]);
    const double mean = std::accumulate(onset.begin(), onset.end(), 0.0) / double(hops);
    for (auto &o : onset) o = std::max(0.0, o - mean);

    // Score each tempo by how well the onsets line up with themselves one and
    // two beats later; lean gently toward 120 BPM to settle half/double-time.
    double bestScore = -1.0, bestBpm = 0.0;
    std::vector<double> scores;
    for (double bpm = 70.0; bpm <= 180.0; bpm += 0.05) {
        const double period = kHopsPerSecond * 60.0 / bpm;
        double s = 0.0;
        for (std::size_t t = 0; t + 2 * period + 1 < double(hops); ++t)
            s += onset[t] * (at(onset, double(t) + period) + 0.5 * at(onset, double(t) + 2.0 * period));
        const double octaves = std::log2(bpm / 120.0);
        s *= std::exp(-0.5 * octaves * octaves / (0.8 * 0.8));
        scores.push_back(s);
        if (s > bestScore) { bestScore = s; bestBpm = bpm; }
    }
    if (bestScore <= 0.0) return est;

    // Coarse phase: the offset within one beat that lands on the most onsets.
    double period = kHopsPerSecond * 60.0 / bestBpm;
    double phase = 0.0, bestHit = -1.0;
    for (double ph = 0.0; ph < period; ph += 0.25) {
        double hit = 0.0;
        for (double t = ph; t < double(hops); t += period) hit += at(onset, t);
        if (hit > bestHit) { bestHit = hit; phase = ph; }
    }

    // Refine: measure each beat's onset in the audio itself at 1 ms
    // resolution — where the level first climbs past half its local peak —
    // and fit a straight line through them (beat number → time). Hop-level
    // onsets are too coarse (10 ms) and scoring tempo with phase lets a
    // slightly wrong tempo win; precision matters, as 0.05 BPM off is ~0.1 s
    // by the end of a four-minute song.
    const auto msFrame = std::max<std::size_t>(1, std::size_t(sampleRate / 1000));
    const std::size_t msFrames = frames / msFrame;
    std::vector<float> env(msFrames);
    for (std::size_t j = 0; j < msFrames; ++j) {
        float m = 0.0f;
        const float *p = mono + j * msFrame;
        for (std::size_t i = 0; i < msFrame; ++i) m = std::max(m, std::abs(p[i]));
        env[j] = m;
    }
    // Frames are msFrame samples (≈1 ms): convert by the real frame length,
    // not 1/1000 s — at 22.05 kHz that's 0.998 ms, and the difference showed
    // up as 128 BPM read as 127.7.
    const double frameSec = double(msFrame) / double(sampleRate);
    auto onsetNear = [&](double seconds, double halfWindow, double *strength) -> double {
        const long long c  = std::llround(seconds / frameSec);
        const long long hw = std::max(2LL, std::llround(halfWindow / frameSec));
        long long peak = -1;
        float peakVal = 0.0f;
        for (long long j = std::max(0LL, c - hw); j <= c + hw && j < (long long)msFrames; ++j)
            if (env[size_t(j)] > peakVal) { peakVal = env[size_t(j)]; peak = j; }
        if (peak < 0) return -1.0;
        long long j = peak;
        while (j > 0 && j > c - 2 * hw && env[size_t(j - 1)] >= 0.5f * peakVal) --j;
        *strength = peakVal;
        return double(j) * frameSec;
    };
    double beatSec = period / kHopsPerSecond, firstSec = phase / kHopsPerSecond;
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<std::pair<double, double>> cand;   // (beat k, onset seconds)
        std::vector<double> strength;
        double strongest = 0.0;
        for (long long k = 0;; ++k) {
            const double predicted = firstSec + double(k) * beatSec;
            if (predicted / frameSec >= double(msFrames) - 1) break;
            double st = 0.0;
            const double t = onsetNear(predicted, beatSec * 0.25, &st);
            if (t < 0.0) continue;
            cand.emplace_back(double(k), t);
            strength.push_back(st);
            strongest = std::max(strongest, st);
        }
        std::vector<std::pair<double, double>> pts;
        for (size_t i = 0; i < cand.size(); ++i)
            if (strength[i] > 0.3 * strongest) pts.push_back(cand[i]);   // beats with a hit
        if (pts.size() < 4) break;
        double sk = 0, st = 0, skk = 0, skt = 0;
        for (const auto &[k, t] : pts) { sk += k; st += t; skk += k * k; skt += k * t; }
        const double n = double(pts.size());
        const double den = n * skk - sk * sk;
        if (std::abs(den) < 1e-12) break;
        const double slope = (n * skt - sk * st) / den;
        if (slope <= 0.0) break;
        beatSec  = slope;
        firstSec = (st - slope * sk) / n;
    }
    period = beatSec * kHopsPerSecond;
    phase  = firstSec * kHopsPerSecond;
    while (phase < 0.0) phase += period;
    while (phase >= period) phase -= period;
    const double bestPhase = phase;
    const double refined = 60.0 * kHopsPerSecond / period;

    std::vector<double> sorted = scores;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const double median = sorted[sorted.size() / 2];

    est.bpm = refined;
    // The onset of hop h is the change into hop h: its time is h hops.
    est.firstBeat = bestPhase / kHopsPerSecond;
    est.confidence = std::clamp((bestScore - median) / bestScore, 0.0, 1.0);
    return est;
}

} // namespace quewi::audio
