#pragma once

#include <QObject>

namespace quewi::audio {

// Owns the audio device callback and the matrix mixer. Real-time hard;
// see structure.md §6 for the rules (no allocs, no mutexes, no Qt API
// calls inside the device callback).
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;
};

} // namespace quewi::audio
