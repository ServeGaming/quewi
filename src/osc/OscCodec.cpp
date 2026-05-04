#include "osc/OscCodec.h"

#include <QtEndian>
#include <cstring>

namespace quewi::osc {

namespace {

// ---------- Low-level write helpers --------------------------------------

void writeI32(QByteArray &out, qint32 v)
{
    qint32 be = qToBigEndian(v);
    out.append(reinterpret_cast<const char *>(&be), 4);
}

void writeI64(QByteArray &out, qint64 v)
{
    qint64 be = qToBigEndian(v);
    out.append(reinterpret_cast<const char *>(&be), 8);
}

void writeU64(QByteArray &out, quint64 v)
{
    quint64 be = qToBigEndian(v);
    out.append(reinterpret_cast<const char *>(&be), 8);
}

void writeF32(QByteArray &out, float v)
{
    static_assert(sizeof(float) == 4);
    quint32 raw;
    std::memcpy(&raw, &v, 4);
    raw = qToBigEndian(raw);
    out.append(reinterpret_cast<const char *>(&raw), 4);
}

void writeF64(QByteArray &out, double v)
{
    static_assert(sizeof(double) == 8);
    quint64 raw;
    std::memcpy(&raw, &v, 8);
    raw = qToBigEndian(raw);
    out.append(reinterpret_cast<const char *>(&raw), 8);
}

void writePadding(QByteArray &out, int rawLen)
{
    const int pad = ((rawLen + 3) & ~3) - rawLen;
    for (int i = 0; i < pad; ++i) out.append('\0');
}

// ---------- Low-level read helpers --------------------------------------

bool readI32(const QByteArray &b, int &cur, qint32 &out)
{
    if (cur + 4 > b.size()) return false;
    qint32 be;
    std::memcpy(&be, b.constData() + cur, 4);
    out = qFromBigEndian(be);
    cur += 4;
    return true;
}

bool readI64(const QByteArray &b, int &cur, qint64 &out)
{
    if (cur + 8 > b.size()) return false;
    qint64 be;
    std::memcpy(&be, b.constData() + cur, 8);
    out = qFromBigEndian(be);
    cur += 8;
    return true;
}

bool readU64(const QByteArray &b, int &cur, quint64 &out)
{
    if (cur + 8 > b.size()) return false;
    quint64 be;
    std::memcpy(&be, b.constData() + cur, 8);
    out = qFromBigEndian(be);
    cur += 8;
    return true;
}

bool readF32(const QByteArray &b, int &cur, float &out)
{
    quint32 be;
    if (cur + 4 > b.size()) return false;
    std::memcpy(&be, b.constData() + cur, 4);
    quint32 host = qFromBigEndian(be);
    std::memcpy(&out, &host, 4);
    cur += 4;
    return true;
}

bool readF64(const QByteArray &b, int &cur, double &out)
{
    quint64 be;
    if (cur + 8 > b.size()) return false;
    std::memcpy(&be, b.constData() + cur, 8);
    quint64 host = qFromBigEndian(be);
    std::memcpy(&out, &host, 8);
    cur += 8;
    return true;
}

// OSC strings: null-terminated, padded to 4-byte boundary.
bool readString(const QByteArray &b, int &cur, QString &out)
{
    int end = cur;
    while (end < b.size() && b[end] != '\0') ++end;
    if (end >= b.size()) return false; // missing terminator
    out = QString::fromUtf8(b.constData() + cur, end - cur);
    cur = (end + 4) & ~3; // skip the NUL plus pad to 4
    return true;
}

bool readBlob(const QByteArray &b, int &cur, QByteArray &out)
{
    qint32 len = 0;
    if (!readI32(b, cur, len) || len < 0) return false;
    if (cur + len > b.size()) return false;
    out = QByteArray(b.constData() + cur, len);
    const int padded = (len + 3) & ~3;
    cur += padded;
    return true;
}

// ---------- Argument encode --------------------------------------------

void writeArg(QByteArray &out, QString &tags, const Argument &a);

void writeArg(QByteArray &out, QString &tags, const Argument &a)
{
    using T = Argument::Tag;
    tags.append(QChar(static_cast<char>(a.tag)));

    switch (a.tag) {
    case T::Int32:    writeI32(out, std::get<qint32>(a.value)); break;
    case T::Float32:  writeF32(out, std::get<float>(a.value)); break;
    case T::String:
    case T::Symbol: {
        const auto &s = std::get<QString>(a.value);
        Codec::appendString(out, s);
        break;
    }
    case T::Blob:
        Codec::appendBlob(out, std::get<QByteArray>(a.value));
        break;
    case T::Int64:    writeI64(out, std::get<qint64>(a.value)); break;
    case T::TimeTag:  writeU64(out, std::get<TimeTag>(a.value).ntp); break;
    case T::Double:   writeF64(out, std::get<double>(a.value)); break;
    case T::Char:     writeI32(out, std::get<qint32>(a.value)); break;
    case T::RgbaColor: {
        const auto c = std::get<QColor>(a.value);
        out.append(static_cast<char>(c.red()));
        out.append(static_cast<char>(c.green()));
        out.append(static_cast<char>(c.blue()));
        out.append(static_cast<char>(c.alpha()));
        break;
    }
    case T::Midi: {
        const auto m = std::get<MidiMessage>(a.value);
        out.append(static_cast<char>(m.portId));
        out.append(static_cast<char>(m.status));
        out.append(static_cast<char>(m.data1));
        out.append(static_cast<char>(m.data2));
        break;
    }
    case T::True:
    case T::False:
    case T::Nil:
    case T::Infinitum:
        // No payload; tag is everything.
        break;
    case T::Array: {
        const auto &arr = std::get<Array>(a.value);
        for (const auto &child : arr) writeArg(out, tags, child);
        tags.append(QChar(']'));
        break;
    }
    }
}

// ---------- Argument decode --------------------------------------------

bool readArg(const QByteArray &b, int &cur, QStringView tags, int &tagIdx, Argument &out);

bool readArg(const QByteArray &b, int &cur, QStringView tags, int &tagIdx, Argument &out)
{
    using T = Argument::Tag;
    if (tagIdx >= tags.size()) return false;
    const QChar tagCh = tags[tagIdx++];
    out.tag = static_cast<T>(tagCh.toLatin1());

    switch (out.tag) {
    case T::Int32: {
        qint32 v; if (!readI32(b, cur, v)) return false;
        out.value = v; return true;
    }
    case T::Float32: {
        float v; if (!readF32(b, cur, v)) return false;
        out.value = v; return true;
    }
    case T::String:
    case T::Symbol: {
        QString s; if (!readString(b, cur, s)) return false;
        out.value = std::move(s); return true;
    }
    case T::Blob: {
        QByteArray bb; if (!readBlob(b, cur, bb)) return false;
        out.value = std::move(bb); return true;
    }
    case T::Int64: {
        qint64 v; if (!readI64(b, cur, v)) return false;
        out.value = v; return true;
    }
    case T::TimeTag: {
        quint64 v; if (!readU64(b, cur, v)) return false;
        out.value = TimeTag{v}; return true;
    }
    case T::Double: {
        double v; if (!readF64(b, cur, v)) return false;
        out.value = v; return true;
    }
    case T::Char: {
        qint32 v; if (!readI32(b, cur, v)) return false;
        out.value = v; return true;
    }
    case T::RgbaColor: {
        if (cur + 4 > b.size()) return false;
        const auto *p = reinterpret_cast<const quint8 *>(b.constData() + cur);
        out.value = QColor(p[0], p[1], p[2], p[3]);
        cur += 4;
        return true;
    }
    case T::Midi: {
        if (cur + 4 > b.size()) return false;
        const auto *p = reinterpret_cast<const quint8 *>(b.constData() + cur);
        out.value = MidiMessage{p[0], p[1], p[2], p[3]};
        cur += 4;
        return true;
    }
    case T::True:
    case T::False:
    case T::Nil:
    case T::Infinitum:
        out.value = std::monostate{};
        return true;
    case T::Array: {
        Array arr;
        while (tagIdx < tags.size() && tags[tagIdx] != QChar(']')) {
            Argument child;
            if (!readArg(b, cur, tags, tagIdx, child)) return false;
            arr.push_back(std::move(child));
        }
        if (tagIdx >= tags.size() || tags[tagIdx] != QChar(']')) return false;
        ++tagIdx; // consume ]
        out.value = std::move(arr);
        return true;
    }
    }
    return false; // unknown tag
}

// ---------- Message / Bundle encode -----------------------------------

QByteArray encodeMessage(const Message &m)
{
    QByteArray out;
    Codec::appendString(out, m.address);

    QString tags;
    tags.reserve(static_cast<int>(m.args.size()) + 1);
    tags.append(QChar(','));

    QByteArray argBytes;
    for (const auto &a : m.args) writeArg(argBytes, tags, a);

    Codec::appendString(out, tags);
    out.append(argBytes);
    return out;
}

QByteArray encodeBundle(const Bundle &b)
{
    QByteArray out;
    Codec::appendString(out, QStringLiteral("#bundle"));
    writeU64(out, b.timeTag.ntp);
    for (const auto &el : b.elements) {
        const QByteArray child = std::visit([](const auto &x) {
            using U = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<U, Message>) return encodeMessage(x);
            else                                     return encodeBundle(x);
        }, el);
        writeI32(out, static_cast<qint32>(child.size()));
        out.append(child);
    }
    return out;
}

// ---------- Message / Bundle decode -----------------------------------

bool decodeMessage(const QByteArray &b, int start, int end, Message &out);
bool decodeBundle(const QByteArray &b, int start, int end, Bundle &out);
bool decodeElement(const QByteArray &b, int start, int end, Element &out);

bool decodeMessage(const QByteArray &b, int start, int end, Message &out)
{
    int cur = start;
    QString address;
    if (!readString(b, cur, address)) return false;
    QString tags;
    if (!readString(b, cur, tags)) return false;
    if (tags.isEmpty() || tags[0] != QChar(',')) return false;

    out.address = std::move(address);
    out.args.clear();

    int tagIdx = 1; // skip leading ','
    while (tagIdx < tags.size()) {
        Argument a;
        if (!readArg(b, cur, tags, tagIdx, a)) return false;
        out.args.push_back(std::move(a));
    }

    return cur <= end;
}

bool decodeBundle(const QByteArray &b, int start, int end, Bundle &out)
{
    int cur = start;
    QString header;
    if (!readString(b, cur, header)) return false;
    if (header != QStringLiteral("#bundle")) return false;

    quint64 t;
    if (!readU64(b, cur, t)) return false;
    out.timeTag = TimeTag{t};
    out.elements.clear();

    while (cur < end) {
        qint32 size = 0;
        if (!readI32(b, cur, size) || size < 0) return false;
        if (cur + size > end) return false;
        Element el;
        if (!decodeElement(b, cur, cur + size, el)) return false;
        out.elements.push_back(std::move(el));
        cur += size;
    }
    return true;
}

bool decodeElement(const QByteArray &b, int start, int end, Element &out)
{
    if (end - start >= 8 && b.mid(start, 8) == QByteArray("#bundle\0", 8)) {
        Bundle bb;
        if (!decodeBundle(b, start, end, bb)) return false;
        out = std::move(bb);
        return true;
    }
    Message m;
    if (!decodeMessage(b, start, end, m)) return false;
    out = std::move(m);
    return true;
}

} // namespace

// ---------- Public API ------------------------------------------------

void Codec::appendString(QByteArray &out, QStringView s)
{
    const QByteArray utf8 = s.toUtf8();
    out.append(utf8);
    out.append('\0');
    writePadding(out, utf8.size() + 1);
}

void Codec::appendBlob(QByteArray &out, const QByteArray &b)
{
    writeI32(out, static_cast<qint32>(b.size()));
    out.append(b);
    writePadding(out, b.size());
}

int Codec::paddedLength(int rawLength)
{
    return (rawLength + 3) & ~3;
}

QByteArray Codec::encode(const Message &m) { return encodeMessage(m); }
QByteArray Codec::encode(const Bundle  &b) { return encodeBundle(b); }
QByteArray Codec::encode(const Element &e)
{
    return std::visit([](const auto &x) {
        using U = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<U, Message>) return encodeMessage(x);
        else                                     return encodeBundle(x);
    }, e);
}

std::optional<Element> Codec::decode(const QByteArray &bytes)
{
    Element el;
    if (!decodeElement(bytes, 0, bytes.size(), el)) return std::nullopt;
    return el;
}

} // namespace quewi::osc
