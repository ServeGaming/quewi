#pragma once

#include "audio/AudioFile.h"
#include <QWidget>
#include <QImage>
#include <vector>
#include <complex>

namespace quewi::ui {

// Renders an FFT spectrogram of a region's audio.
// Call setSource() with a loaded AudioFile and the in/out sample range to analyse.
// The FFT is computed once then cached as a QImage; re-rendered on resize.
class SpectrogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrogramWidget(QWidget *parent = nullptr);
    ~SpectrogramWidget() override = default;

    void setSource(std::shared_ptr<audio::AudioFile> file, qint64 inSample, qint64 outSample);
    void clear();

    // The owner toggles this when the user shows/hides the Spectrogram tab.
    // While inactive, source changes are buffered but no FFT runs — keeps
    // region-selection click latency at zero.
    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void buildSpectrogram();
    static float hzToY(float hz, float maxHz, int height);
    static QRgb amplitudeToColor(float db);

    std::shared_ptr<audio::AudioFile> m_file;
    qint64 m_inSample  = 0;
    qint64 m_outSample = 0;
    bool   m_active    = false;

    static constexpr int kFftSize = 2048;

    // Magnitude spectrum per time column: m_spec[col * (kFftSize/2+1)] = magnitude at bin
    std::vector<float> m_spec;
    int   m_specCols   = 0;
    QImage m_cache;
    bool  m_dirty      = false;

    static void fft(std::vector<std::complex<float>> &a);
};

} // namespace quewi::ui
