#include "midi/MidiInputEngine.h"

#include <RtMidi.h>

#include <QMetaObject>

namespace quewi::midi {

MidiInputEngine::MidiInputEngine(QObject *parent) : QObject(parent) {}

MidiInputEngine::~MidiInputEngine()
{
    if (m_in) {
        m_in->cancelCallback();
        m_in->closePort();
        m_in.reset();
    }
}

void MidiInputEngine::refreshPorts() const
{
    m_cachedPorts.clear();
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "quewi");
        const auto n = probe.getPortCount();
        for (unsigned i = 0; i < n; ++i)
            m_cachedPorts << QString::fromStdString(probe.getPortName(i));
    } catch (const RtMidiError &e) {
        m_lastError = QString::fromStdString(e.getMessage());
    }
    m_cacheTimer.start();
}

QStringList MidiInputEngine::inputPortNames() const
{
    if (!m_cacheTimer.isValid() || m_cacheTimer.elapsed() > 5000)
        refreshPorts();
    return m_cachedPorts;
}

bool MidiInputEngine::openPort(const QString &portName)
{
    m_lastError.clear();

    // Close current port if any.
    if (m_in) {
        m_in->cancelCallback();
        m_in->closePort();
        m_in.reset();
        m_openPortName.clear();
    }
    if (portName.isEmpty()) return true;   // explicit "no port"

    try {
        m_in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "quewi");
        // Route system-realtime aside — we don't need clock / active-sense
        // noise for trigger bindings, and ignoring them lightens the
        // dispatcher.
        m_in->ignoreTypes(true /*sysex*/, true /*timing*/, true /*active-sense*/);

        const unsigned n = m_in->getPortCount();
        unsigned chosen = unsigned(-1);
        for (unsigned i = 0; i < n; ++i) {
            if (QString::fromStdString(m_in->getPortName(i)) == portName) {
                chosen = i; break;
            }
        }
        if (chosen == unsigned(-1)) {
            m_lastError = tr("Port not found: %1").arg(portName);
            m_in.reset();
            return false;
        }
        m_in->openPort(chosen, "quewi-in");
        m_in->setCallback(&MidiInputEngine::onMidiCallback, this);
        m_openPortName = portName;
        return true;
    } catch (const RtMidiError &e) {
        m_lastError = QString::fromStdString(e.getMessage());
        m_in.reset();
        return false;
    }
}

void MidiInputEngine::onMidiCallback(double /*ts*/,
                                     std::vector<unsigned char> *message,
                                     void *userData)
{
    if (!message || message->empty() || !userData) return;
    auto *self = static_cast<MidiInputEngine *>(userData);
    QByteArray bytes(reinterpret_cast<const char *>(message->data()),
                     int(message->size()));
    const quint8 status = quint8(bytes[0]);
    // RtMidi callback runs on its own thread; marshal to the QObject's
    // owning thread via QueuedConnection so subscribers don't have to
    // worry about thread safety.
    QMetaObject::invokeMethod(self, "messageReceived", Qt::QueuedConnection,
                              Q_ARG(quint8, status),
                              Q_ARG(QByteArray, bytes));
}

} // namespace quewi::midi
