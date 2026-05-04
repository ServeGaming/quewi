#pragma once

#include <QObject>

namespace quewi::midi {

// RtMidi-backed device I/O plus hand-rolled MSC (MIDI Show Control) framing.
class MidiEngine : public QObject {
    Q_OBJECT
public:
    explicit MidiEngine(QObject *parent = nullptr);
    ~MidiEngine() override;
};

} // namespace quewi::midi
