#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariant>
#include <variant>
#include <vector>

// quewi's OSC engine — hand-written, full OSC 1.0 + 1.1 + common practice.
// See docs/osc-coverage.md for the spec coverage matrix.
//
// Supported transports: UDP, TCP (with SLIP framing per RFC 1055), WebSocket,
// Unix domain sockets, multicast/broadcast. OSC Query (HTTP/WebSocket
// namespace introspection) is layered on top.
//
// Supported type tags: i f s b   h t d S c r m T F N I [ ]
//   i  int32          T  true
//   f  float32        F  false
//   s  string         N  nil
//   b  blob           I  infinitum
//   h  int64          [  array begin
//   t  OSC time tag   ]  array end
//   d  double
//   S  symbol
//   c  char
//   r  RGBA color
//   m  MIDI message
//
// Pattern matching: ?, *, [chars], {alt,alt}, // (1.1 descendant)

namespace quewi::osc {

// One OSC argument value, holding any of the supported type tags.
struct Argument {
    enum class Tag : char {
        Int32     = 'i',
        Float32   = 'f',
        String    = 's',
        Blob      = 'b',
        Int64     = 'h',
        TimeTag   = 't',
        Double    = 'd',
        Symbol    = 'S',
        Char      = 'c',
        RgbaColor = 'r',
        Midi      = 'm',
        True      = 'T',
        False     = 'F',
        Nil       = 'N',
        Infinitum = 'I',
        ArrayOpen = '[',
        ArrayClose = ']',
    };
    Tag tag;
    QVariant value;
};

// 64-bit NTP-format OSC time tag. Sentinel 0x0000000000000001 = "immediately".
struct TimeTag {
    quint64 ntp = 1;
    static TimeTag immediate() { return {1}; }
};

struct Message {
    QString address;
    std::vector<Argument> args;
};

struct Bundle;
using Element = std::variant<Message, Bundle>;

struct Bundle {
    TimeTag timeTag;
    std::vector<Element> elements;
};

// Codec — pure functions, no I/O. Phase 2 implementation.
class Codec {
public:
    static QByteArray encode(const Message &m);
    static QByteArray encode(const Bundle &b);
    // Returns Message or Bundle on success; empty Element on parse failure.
    static std::optional<Element> decode(const QByteArray &bytes);
};

// The engine — holds transports, dispatches incoming messages by pattern,
// schedules outgoing bundles by time tag.
class OscEngine : public QObject {
    Q_OBJECT
public:
    explicit OscEngine(QObject *parent = nullptr);
    ~OscEngine() override;

    // Phase 2 will add: addUdpInput, addTcpInput, addWsInput,
    //                   addUdpOutput, addTcpOutput, addWsOutput,
    //                   send(destination, message_or_bundle),
    //                   subscribe(pattern, handler).
};

} // namespace quewi::osc
