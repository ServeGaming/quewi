#pragma once

#include <QByteArray>
#include <QChar>
#include <QColor>
#include <QString>
#include <QtGlobal>
#include <variant>
#include <vector>

// quewi OSC value model — pure data, no I/O.
//
// One Argument holds any OSC 1.0 / 1.1 / common-practice type. The Tag
// matches the OSC type tag character so encoding back to bytes is
// straightforward.
//
// Type tags supported:
//   i  int32              T  true (no payload)
//   f  float32             F  false (no payload)
//   s  string              N  nil (no payload)
//   b  blob                I  infinitum (no payload)
//   h  int64               [  array begin
//   t  OSC time tag        ]  array end
//   d  double
//   S  symbol (ASCII string with semantic distinction from 's')
//   c  char (32-bit, treated as ASCII codepoint)
//   r  RGBA color (4 bytes)
//   m  MIDI message (4 bytes: port id, status, data1, data2)

namespace quewi::osc {

// 64-bit NTP-format time tag. Sentinel 0x0000000000000001 = "execute now".
struct TimeTag {
    quint64 ntp = 1;
    static constexpr TimeTag immediate() { return {1}; }
    bool isImmediate() const { return ntp == 1; }
};

// 4-byte raw MIDI message (port id, status, data1, data2).
struct MidiMessage {
    quint8 portId = 0;
    quint8 status = 0;
    quint8 data1  = 0;
    quint8 data2  = 0;
};

struct Argument;
using Array = std::vector<Argument>;

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
        Array     = '[',  // tag for nested array; close ']' is implicit
    };

    Tag tag;
    std::variant<
        std::monostate,  // no-payload tags (T F N I)
        qint32,          // i, c (char encoded as 32-bit)
        float,           // f
        QString,         // s, S
        QByteArray,      // b
        qint64,          // h
        TimeTag,         // t
        double,          // d
        QColor,          // r
        MidiMessage,     // m
        Array            // [
    > value;

    // Convenience constructors
    static Argument i(qint32 v)             { return {Tag::Int32,     v}; }
    static Argument f(float v)              { return {Tag::Float32,   v}; }
    static Argument s(QString v)            { return {Tag::String,    std::move(v)}; }
    static Argument b(QByteArray v)         { return {Tag::Blob,      std::move(v)}; }
    static Argument h(qint64 v)             { return {Tag::Int64,     v}; }
    static Argument t(TimeTag v)            { return {Tag::TimeTag,   v}; }
    static Argument d(double v)             { return {Tag::Double,    v}; }
    static Argument symbol(QString v)       { return {Tag::Symbol,    std::move(v)}; }
    static Argument c(qint32 codepoint)     { return {Tag::Char,      codepoint}; }
    static Argument r(QColor v)             { return {Tag::RgbaColor, v}; }
    static Argument m(MidiMessage v)        { return {Tag::Midi,      v}; }
    static Argument T()                     { return {Tag::True,      std::monostate{}}; }
    static Argument F()                     { return {Tag::False,     std::monostate{}}; }
    static Argument N()                     { return {Tag::Nil,       std::monostate{}}; }
    static Argument I()                     { return {Tag::Infinitum, std::monostate{}}; }
    static Argument array(Array v)          { return {Tag::Array,     std::move(v)}; }
};

struct Message {
    QString address;
    std::vector<Argument> args;
};

struct Bundle;
using Element = std::variant<Message, Bundle>;

struct Bundle {
    TimeTag timeTag = TimeTag::immediate();
    std::vector<Element> elements;
};

} // namespace quewi::osc
