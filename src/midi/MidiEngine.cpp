#include "midi/MidiEngine.h"

#include <RtMidi.h>

#include <vector>

namespace quewi::midi {

MidiEngine::MidiEngine(QObject *parent) : QObject(parent) {}
MidiEngine::~MidiEngine() = default;

QStringList MidiEngine::outputPortNames() const
{
    QStringList out;
    try {
        RtMidiOut probe(RtMidi::UNSPECIFIED, "quewi");
        const auto n = probe.getPortCount();
        for (unsigned i = 0; i < n; ++i) {
            out << QString::fromStdString(probe.getPortName(i));
        }
    } catch (const RtMidiError &) {
        // Return whatever we collected; engine fall-back will report.
    }
    return out;
}

RtMidiOut *MidiEngine::openOrGetPort(const QString &portName)
{
    if (m_out && m_openPortName == portName) return m_out.get();

    try {
        auto next = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "quewi");
        const auto n = next->getPortCount();
        if (n == 0) {
            m_lastError = tr("No MIDI output ports available");
            return nullptr;
        }
        unsigned chosen = 0;
        if (!portName.isEmpty()) {
            bool found = false;
            for (unsigned i = 0; i < n; ++i) {
                if (QString::fromStdString(next->getPortName(i)) == portName) {
                    chosen = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_lastError = tr("MIDI port '%1' not found").arg(portName);
                return nullptr;
            }
        }
        next->openPort(chosen, "quewi-out");
        m_out = std::move(next);
        m_openPortName = portName.isEmpty()
            ? QString::fromStdString(m_out->getPortName(chosen))
            : portName;
        return m_out.get();
    } catch (const RtMidiError &e) {
        m_lastError = QString::fromStdString(e.getMessage());
        return nullptr;
    }
}

bool MidiEngine::sendRaw(const QString &portName, const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        m_lastError = tr("Empty MIDI message");
        return false;
    }
    auto *port = openOrGetPort(portName);
    if (!port) return false;

    std::vector<unsigned char> v;
    v.reserve(bytes.size());
    for (auto b : bytes) v.push_back(static_cast<unsigned char>(b));
    try {
        port->sendMessage(&v);
        return true;
    } catch (const RtMidiError &e) {
        m_lastError = QString::fromStdString(e.getMessage());
        return false;
    }
}

bool MidiEngine::sendMsc(const QString &portName,
                         quint8 deviceId,
                         quint8 commandFormat,
                         quint8 command,
                         const QByteArray &payload)
{
    // MIDI Show Control sysex per MMA RP-002:
    //   F0 7F <deviceID> 02 <command_format> <command> <data...> F7
    QByteArray msg;
    msg.append(static_cast<char>(0xF0));
    msg.append(static_cast<char>(0x7F));
    msg.append(static_cast<char>(deviceId & 0x7F));
    msg.append(static_cast<char>(0x02));
    msg.append(static_cast<char>(commandFormat & 0x7F));
    msg.append(static_cast<char>(command & 0x7F));
    msg.append(payload);
    msg.append(static_cast<char>(0xF7));
    return sendRaw(portName, msg);
}

} // namespace quewi::midi
