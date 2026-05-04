#include "ui/SpectrogramWidget.h"
#include <QPainter>
#include <cmath>
#include <algorithm>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::ui {

SpectrogramWidget::SpectrogramWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(120);
}

void SpectrogramWidget::setSource(std::shared_ptr<audio::AudioFile> file,
                                  qint64 in, qint64 out) {
    m_file     = file;
    m_inSample = in;
    m_outSample = (out < 0 && file) ? file->frameCount() : out;
    m_dirty    = true;
    m_cache    = QImage();
    if (m_active) update();
}

void SpectrogramWidget::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    if (active) update();
}

void SpectrogramWidget::clear() {
    m_file.reset();
    m_spec.clear();
    m_cache = QImage();
    update();
}

// Radix-2 Cooley-Tukey FFT in-place
void SpectrogramWidget::fft(std::vector<std::complex<float>> &a) {
    int n = int(a.size());
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    // Butterfly stages
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.f * M_PIf / float(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (int j = 0; j < len/2; ++j) {
                auto u = a[i+j], v = a[i+j+len/2] * w;
                a[i+j]       = u + v;
                a[i+j+len/2] = u - v;
                w *= wlen;
            }
        }
    }
}

void SpectrogramWidget::buildSpectrogram() {
    m_spec.clear();
    m_specCols = 0;
    if (!m_file || m_file->state() != audio::AudioFile::State::Loaded) return;

    const auto &samples = m_file->samples();
    int ch = m_file->channelCount();
    qint64 inF  = m_inSample;
    qint64 outF = std::min(m_outSample, m_file->frameCount());
    qint64 dur  = outF - inF;
    if (dur <= 0) return;

    // Cap the number of FFT columns so the build stays under ~50ms even
    // for hour-long files. We adapt the hop size to the duration.
    constexpr int kMaxCols = 1024;
    int hop = kFftSize / 4;
    if ((dur - kFftSize) / hop > kMaxCols)
        hop = int((dur - kFftSize) / kMaxCols);
    int numCols = int((dur - kFftSize) / std::max(1, hop));
    numCols = std::clamp(numCols, 1, kMaxCols);

    int numBins = kFftSize / 2 + 1;
    m_spec.resize(size_t(numCols * numBins), 0.f);

    // Hann window
    std::vector<float> window(kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[i] = 0.5f * (1.f - std::cos(2.f * M_PIf * i / float(kFftSize - 1)));

    std::vector<std::complex<float>> buf(kFftSize);

    for (int col = 0; col < numCols; ++col) {
        qint64 startF = inF + qint64(col) * hop;
        for (int i = 0; i < kFftSize; ++i) {
            qint64 f = startF + i;
            float s = (f < outF) ? samples[size_t(f * ch)] : 0.f; // mono-downmix
            if (ch >= 2 && f < outF)
                s = (s + samples[size_t(f * ch + 1)]) * 0.5f;
            buf[i] = std::complex<float>(s * window[i], 0.f);
        }
        fft(buf);
        for (int b = 0; b < numBins; ++b) {
            float mag = std::abs(buf[b]) / float(kFftSize);
            m_spec[size_t(col * numBins + b)] = mag;
        }
    }
    m_specCols = numCols;
    m_dirty = false;
}

QRgb SpectrogramWidget::amplitudeToColor(float db) {
    // Map -80 dB … 0 dB → blue … cyan → green → yellow → red
    float t = std::clamp((db + 80.f) / 80.f, 0.f, 1.f);
    // Simple heat map
    float r = std::clamp(t * 4.f - 2.f, 0.f, 1.f);
    float g = std::clamp(std::min(t * 4.f, 4.f - t * 4.f), 0.f, 1.f);
    float b = std::clamp(1.f - t * 4.f, 0.f, 1.f);
    return qRgb(int(r*255), int(g*255), int(b*255));
}

void SpectrogramWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(10, 12, 18));

    if (!m_file) {
        p.setPen(QColor(80, 90, 110));
        p.drawText(rect(), Qt::AlignCenter, tr("No audio selected"));
        return;
    }
    if (!m_active) {
        p.setPen(QColor(80, 90, 110));
        p.drawText(rect(), Qt::AlignCenter, tr("Spectrogram (open this tab to compute)"));
        return;
    }

    if (m_dirty) buildSpectrogram();
    if (m_specCols <= 0) return;

    // Render spectrogram image lazily (cache invalidated by resize)
    if (m_cache.size() != size()) {
        int numBins = kFftSize / 2 + 1;
        m_cache = QImage(m_specCols, height(), QImage::Format_RGB32);
        m_cache.fill(Qt::black);

        float maxHz = float(m_file->sampleRate()) / 2.f;
        for (int col = 0; col < m_specCols; ++col) {
            for (int y = 0; y < height(); ++y) {
                // Map y to frequency (linear for now)
                float hz = maxHz * float(height() - 1 - y) / float(height() - 1);
                int   bin = int(hz / maxHz * float(numBins - 1));
                bin = std::clamp(bin, 0, numBins - 1);
                float mag = m_spec[size_t(col * numBins + bin)];
                float db  = (mag > 1e-9f) ? 20.f * std::log10(mag) : -80.f;
                m_cache.setPixel(col, y, amplitudeToColor(db));
            }
        }
    }

    p.drawImage(QRect(0, 0, width(), height()),
                m_cache, QRect(0, 0, m_cache.width(), m_cache.height()));

    // Frequency labels
    p.setPen(QColor(180, 190, 210));
    p.setFont(QFont(font().family(), 7));
    float maxHz = m_file ? float(m_file->sampleRate()) / 2.f : 22050.f;
    static const float kLabelHz[] = {100, 500, 1000, 5000, 10000, 20000};
    for (float hz : kLabelHz) {
        if (hz > maxHz) break;
        int y = int(float(height()) * (1.f - hz / maxHz));
        p.drawText(2, y - 1, QStringLiteral("%1").arg(hz < 1000 ? QString::number(int(hz)) + QStringLiteral("Hz")
                                                                 : QString::number(int(hz/1000)) + QStringLiteral("k")));
    }
}

void SpectrogramWidget::resizeEvent(QResizeEvent *) {
    m_cache = QImage(); // invalidate cache
    update();
}

} // namespace quewi::ui
