#include "mix/Dm7Value.h"

#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace quewi::mix::dm7 {
namespace {

// Yamaha Table 2: the only legal pan positions.
constexpr std::array<int, 27> kLegalPans = {
    -63, -60, -55, -50, -45, -40, -35, -30, -25, -20, -15, -10, -5,
      0,
      5,  10,  15,  20,  25,  30,  35,  40,  45,  50,  55,  60,  63
};

} // namespace

// ── Line framing ─────────────────────────────────────────────────────

QStringList tokenize(const QString &line)
{
    QStringList out;
    QString current;
    bool inQuotes = false;
    bool escaped  = false;
    bool building = false;   // distinguishes "" (a real empty token) from a gap

    for (const QChar c : line) {
        if (escaped) {
            current += c;             // \\ -> \, \" -> "
            escaped = false;
            continue;
        }
        if (c == u'\\') { escaped = true; building = true; continue; }
        if (c == u'"')  { inQuotes = !inQuotes; building = true; continue; }

        if (!inQuotes && c.isSpace()) {
            if (building) { out.push_back(current); current.clear(); building = false; }
            continue;
        }
        current += c;
        building = true;
    }
    if (building) out.push_back(current);
    return out;
}

QString quote(const QString &s)
{
    QString escaped;
    escaped.reserve(s.size() + 2);
    for (const QChar c : s) {
        if (c == u'\\' || c == u'"') escaped += u'\\';
        escaped += c;
    }
    return u'"' + escaped + u'"';
}

std::optional<Reply> parseReply(const QString &line)
{
    const auto tokens = tokenize(line.trimmed());
    if (tokens.size() < 2) return std::nullopt;

    Reply r;
    const QString &status = tokens[0];
    if      (status == QLatin1String("OK"))     r.status = Reply::Status::Ok;
    else if (status == QLatin1String("OKm"))    r.status = Reply::Status::OkModified;
    else if (status == QLatin1String("NOTIFY")) r.status = Reply::Status::Notify;
    else if (status == QLatin1String("ERROR"))  r.status = Reply::Status::Error;
    else return std::nullopt;       // not a reply line at all

    r.action = tokens[1];

    // ERROR <command> <code> — no address/index fields.
    if (r.status == Reply::Status::Error) {
        if (tokens.size() >= 3) r.value = tokens[2];
        return r;
    }

    // Everything else may or may not carry an address. `devinfo productname
    // "DM7"` has no X/Y, while a parameter reply has the full set.
    if (tokens.size() >= 3) r.address = tokens[2];

    // Parameter replies: Address X Y Val [TxtVal]. Only treat tokens 3 and 4
    // as indices if they really are integers — otherwise this is a devinfo-
    // style reply whose token 3 is the value.
    if (tokens.size() >= 5) {
        bool xOk = false, yOk = false;
        const int x = tokens[3].toInt(&xOk);
        const int y = tokens[4].toInt(&yOk);
        if (xOk && yOk) {
            r.x = x;
            r.y = y;
            if (tokens.size() >= 6) r.value     = tokens[5];
            if (tokens.size() >= 7) r.textValue = tokens[6];
            return r;
        }
    }
    if (tokens.size() >= 4) r.value = tokens[3];
    return r;
}

// ── Commands ─────────────────────────────────────────────────────────

QString setCommand(const QString &address, int x0, int y0, const QString &rawValue)
{
    return QStringLiteral("set %1 %2 %3 %4").arg(address).arg(x0).arg(y0).arg(rawValue);
}

QString getCommand(const QString &address, int x0, int y0)
{
    return QStringLiteral("get %1 %2 %3").arg(address).arg(x0).arg(y0);
}

// ── dB ───────────────────────────────────────────────────────────────

int dbToRaw(float db)
{
    if (std::isinf(db) && db < 0) return kLevelNegInf;
    const int raw = int(std::lround(db * 100.0f));
    if (raw <= kLevelMinReal) return kLevelNegInf;   // below the lowest detent
    return std::min(raw, kLevelMax);
}

float rawToDb(int raw)
{
    if (raw <= kLevelNegInf) return -std::numeric_limits<float>::infinity();
    return float(raw) / 100.0f;
}

// ── Pan ──────────────────────────────────────────────────────────────

bool isLegalPan(int pan)
{
    return std::find(kLegalPans.begin(), kLegalPans.end(), pan) != kLegalPans.end();
}

int snapPan(int pan)
{
    const int clamped = std::clamp(pan, kLegalPans.front(), kLegalPans.back());
    const auto nearest = std::min_element(
        kLegalPans.begin(), kLegalPans.end(),
        [clamped](int a, int b) { return std::abs(a - clamped) < std::abs(b - clamped); });
    return *nearest;
}

} // namespace quewi::mix::dm7
