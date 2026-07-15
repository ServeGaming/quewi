#include "mix/X32Value.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quewi::mix::x32 {
namespace {

float clamp01(float f) { return std::clamp(f, 0.0f, 1.0f); }

QString padded(int value, int width)
{
    return QString::number(value).rightJustified(width, u'0');
}

QString join(QString base, QLatin1String suffix)
{
    if (suffix.isEmpty()) return base;
    return base + u'/' + suffix;
}

} // namespace

// ── DCA membership ───────────────────────────────────────────────────

QVector<int> dcaMaskToList(DcaMask mask)
{
    QVector<int> out;
    for (int dca = 1; dca <= kDcaCount; ++dca)
        if (dcaMaskContains(mask, dca)) out.push_back(dca);
    return out;
}

DcaMask dcaListToMask(const QVector<int> &dcas)
{
    DcaMask mask = 0;
    for (int dca : dcas) mask |= dcaBit(dca);   // out-of-range yields 0
    return mask;
}

std::optional<DcaMask> parseMaskText(QStringView text)
{
    if (!text.startsWith(u'%')) return std::nullopt;
    const QStringView bits = text.mid(1);
    if (bits.isEmpty() || bits.size() > 8) return std::nullopt;

    // MSB-first, i.e. ordinary binary. Width-tolerant so the same parser
    // handles the console's fixed "%00000010" and the emulator's "%10".
    DcaMask mask = 0;
    for (QChar c : bits) {
        if (c != u'0' && c != u'1') return std::nullopt;
        mask = DcaMask((mask << 1) | (c == u'1' ? 1 : 0));
    }
    return mask;
}

QString maskText(DcaMask mask, int width)
{
    QString bits;
    for (int bit = width - 1; bit >= 0; --bit)
        bits += (mask & (1u << bit)) ? u'1' : u'0';
    return u'%' + bits;
}

// ── Addresses ────────────────────────────────────────────────────────

QString chAddr(int channel, QLatin1String suffix)
{
    return join(QStringLiteral("/ch/") + padded(channel, 2), suffix);
}

QString dcaAddr(int dca, QLatin1String suffix)
{
    // Single digit — deliberately not padded. See the header.
    return join(QStringLiteral("/dca/") + QString::number(dca), suffix);
}

QString headampAddr(int headamp, QLatin1String suffix)
{
    return join(QStringLiteral("/headamp/") + padded(headamp, 3), suffix);
}

// ── level ────────────────────────────────────────────────────────────

float levelToDb(float f)
{
    f = clamp01(f);
    if (f == 0.0f)     return -std::numeric_limits<float>::infinity();
    if (f >= 0.5f)     return f * 40.0f  - 30.0f;   // max +10 dB
    if (f >= 0.25f)    return f * 80.0f  - 50.0f;
    if (f >= 0.0625f)  return f * 160.0f - 70.0f;
    return f * 480.0f - 90.0f;
}

float dbToLevel(float db)
{
    if (db <= -90.0f) return 0.0f;      // covers -inf; hard off
    if (db < -60.0f)  return (db + 90.0f)  / 480.0f;
    if (db < -30.0f)  return (db + 70.0f)  / 160.0f;
    if (db < -10.0f)  return (db + 50.0f)  / 80.0f;
    if (db <= 10.0f)  return (db + 30.0f)  / 40.0f;
    return 1.0f;                        // +10 dB is the ceiling
}

float quantizeFader(float f)
{
    // Verbatim from the protocol doc's rounding for 1024-step controls.
    return float(int(clamp01(f) * 1023.5f)) / 1023.0f;
}

float quantizeSend(float f)
{
    return quantizeSteps(f, 161);
}

// ── linf / logf ──────────────────────────────────────────────────────

float linfToValue(float f, float min, float max)
{
    return min + clamp01(f) * (max - min);
}

float valueToLinf(float value, float min, float max)
{
    if (min == max) return 0.0f;
    return clamp01((value - min) / (max - min));
}

float logfToValue(float f, float min, float max)
{
    if (min <= 0.0f || max <= 0.0f) return min;
    return min * std::pow(max / min, clamp01(f));
}

float valueToLogf(float value, float min, float max)
{
    if (min <= 0.0f || max <= 0.0f || value <= 0.0f || min == max) return 0.0f;
    return clamp01(std::log(value / min) / std::log(max / min));
}

float quantizeSteps(float f, int steps)
{
    if (steps <= 1) return clamp01(f);
    const float n = float(steps - 1);
    return std::round(clamp01(f) * n) / n;
}

// ── EQ ───────────────────────────────────────────────────────────────

float eqFreqToFloat(float hz)
{
    return quantizeSteps(valueToLogf(hz, kEqFreqMin, kEqFreqMax), kEqFreqSteps);
}

float floatToEqFreq(float f)
{
    return logfToValue(f, kEqFreqMin, kEqFreqMax);
}

float eqGainToFloat(float db)
{
    return valueToLinf(db, kEqGainMin, kEqGainMax);
}

float floatToEqGain(float f)
{
    return linfToValue(f, kEqGainMin, kEqGainMax);
}

// min > max on purpose — the mapping is inverted. See the header.
float eqQToFloat(float q)
{
    return quantizeSteps(valueToLogf(q, kEqQMin, kEqQMax), kEqQSteps);
}

float floatToEqQ(float f)
{
    return logfToValue(f, kEqQMin, kEqQMax);
}

} // namespace quewi::mix::x32
