#include "audio/Cuts.h"

#include <QJsonDocument>

#include <algorithm>

namespace quewi::audio {

namespace {
constexpr double kMinCut = 0.01;   // shorter than this isn't worth a jump
}

Cuts normalizeCuts(Cuts cuts)
{
    for (auto &c : cuts) {
        c.start = std::max(0.0, c.start);
        c.end   = std::max(0.0, c.end);
        if (c.end < c.start) std::swap(c.start, c.end);
    }
    std::sort(cuts.begin(), cuts.end(),
              [](const Cut &a, const Cut &b) { return a.start < b.start; });
    Cuts out;
    for (const auto &c : cuts) {
        if (!out.empty() && c.start <= out.back().end) out.back().end = std::max(out.back().end, c.end);
        else out.push_back(c);
    }
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Cut &c) { return c.length() < kMinCut; }),
              out.end());
    return out;
}

QJsonArray cutsToJson(const Cuts &cuts)
{
    QJsonArray a;
    for (const auto &c : cuts) a.append(QJsonArray{c.start, c.end});
    return a;
}

Cuts cutsFromVariant(const QVariant &v, bool *ok)
{
    if (ok) *ok = true;
    QJsonArray a;
    if (v.metaType() == QMetaType::fromType<QJsonArray>()) {
        a = v.toJsonArray();
    } else if (v.metaType() == QMetaType::fromType<QVariantList>()) {
        a = QJsonArray::fromVariantList(v.toList());
    } else {
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) return {};
        const auto doc = QJsonDocument::fromJson(s.toUtf8());
        if (!doc.isArray()) { if (ok) *ok = false; return {}; }
        a = doc.array();
    }
    Cuts out;
    for (const auto &e : a) {
        const auto pair = e.toArray();
        if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble()) {
            if (ok) *ok = false;
            return {};
        }
        out.push_back({pair[0].toDouble(), pair[1].toDouble()});
    }
    return normalizeCuts(std::move(out));
}

Cuts addCut(const Cuts &cuts, double start, double end)
{
    Cuts next = cuts;
    next.push_back({std::min(start, end), std::max(start, end)});
    return normalizeCuts(std::move(next));
}

Cuts removeCut(const Cuts &cuts, double start, double end)
{
    const double a = std::min(start, end), b = std::max(start, end);
    Cuts out;
    for (const auto &c : cuts) {
        if (c.end <= a || c.start >= b) { out.push_back(c); continue; }
        if (c.start < a) out.push_back({c.start, a});   // the part before
        if (c.end > b)   out.push_back({b, c.end});     // the part after
    }
    return normalizeCuts(std::move(out));
}

const Cut *cutAt(const Cuts &cuts, double seconds)
{
    for (const auto &c : cuts)
        if (seconds >= c.start && seconds < c.end) return &c;
    return nullptr;
}

double cutSecondsBetween(const Cuts &cuts, double from, double to)
{
    double total = 0.0;
    for (const auto &c : cuts) {
        const double a = std::max(from, c.start), b = std::min(to, c.end);
        if (b > a) total += b - a;
    }
    return total;
}

} // namespace quewi::audio
