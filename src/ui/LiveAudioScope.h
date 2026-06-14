#pragma once

#include <QObject>
#include <vector>

namespace quewi::ui {

// A lightweight real-time analyzer that the effect editors (EQ, Compressor)
// subscribe to so they can draw a live spectrum / level while the editor
// previews audio — the way a DM7-style console overlays an analyzer on its
// channel EQ.
//
// The audio editor calls analyze() once per playback tick with the rendered
// mix and the current playback frame; the scope computes an FFT magnitude
// spectrum plus peak/RMS level for the window at that position and emits
// updated(). Everything runs on the GUI thread (it reads an already-rendered
// buffer — there is no audio-callback involvement), so no locking is needed.
class LiveAudioScope : public QObject {
    Q_OBJECT
public:
    explicit LiveAudioScope(QObject *parent = nullptr);
    ~LiveAudioScope() override = default;

    static constexpr int kFftSize = 2048;

    // Analyze a 2048-frame window of `interleavedStereo` starting at
    // `frameOffset`. No-op (and goes inactive) if the buffer is too short.
    void analyze(const std::vector<float> &interleavedStereo,
                 qint64 frameOffset, int sampleRate);

    // Called when playback stops — clears the active flag and notifies so
    // subscribers can fade the analyzer out.
    void setInactive();

    bool   active()     const { return m_active; }
    int    sampleRate() const { return m_sampleRate; }
    int    fftSize()    const { return kFftSize; }
    float  peakDb()     const { return m_peakDb; }   // dBFS of the window
    float  rmsDb()      const { return m_rmsDb; }
    // Linear magnitude per FFT bin, size == kFftSize/2 + 1. Empty until the
    // first analyze().
    const std::vector<float> &magnitudes() const { return m_mag; }

signals:
    void updated();

private:
    bool   m_active     = false;
    int    m_sampleRate = 48000;
    float  m_peakDb     = -120.f;
    float  m_rmsDb      = -120.f;
    std::vector<float> m_mag;
    std::vector<float> m_window; // Hann, built lazily
};

} // namespace quewi::ui
