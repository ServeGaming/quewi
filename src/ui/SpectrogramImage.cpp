#include "ui/SpectrogramImage.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::ui::spectro {

namespace {

// Radix-2 Cooley-Tukey FFT, in-place. (Same kernel the spectrogram tab uses;
// kept local so this builder has no dependency on any widget.)
void fft(std::vector<std::complex<float>> &a) {
    const int n = int(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.f * M_PIf / float(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (int k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// -90 … 0 dB → blue → cyan → green → yellow → red heat map. Matches the
// spectrogram tab's palette so the two views read consistently.
QRgb heat(float db) {
    const float t = std::clamp((db + 90.f) / 90.f, 0.f, 1.f);
    const float r = std::clamp(t * 4.f - 2.f, 0.f, 1.f);
    const float g = std::clamp(std::min(t * 4.f, 4.f - t * 4.f), 0.f, 1.f);
    const float b = std::clamp(1.f - t * 4.f, 0.f, 1.f);
    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

} // namespace

QImage buildFullFile(const std::shared_ptr<const audio::AudioBufferSnapshot> &snap,
                     int rows, int maxCols) {
    if (!snap || !snap->samples) return {};
    const std::vector<float> &s = *snap->samples;
    const int    ch     = snap->channelCount;
    const qint64 frames = snap->frameCount;
    if (ch <= 0 || frames <= 0) return {};

    constexpr int N = 2048;
    constexpr int numBins = N / 2 + 1;
    if (frames < N) return {};

    rows = std::clamp(rows, 64, 2048);

    int hop = N / 2;
    if ((frames - N) / hop > maxCols)
        hop = int((frames - N) / maxCols);
    hop = std::max(1, hop);
    int cols = int((frames - N) / hop);
    cols = std::clamp(cols, 1, maxCols);

    // Hann window.
    std::vector<float> window(N);
    for (int i = 0; i < N; ++i)
        window[i] = 0.5f * (1.f - std::cos(2.f * M_PIf * i / float(N - 1)));

    // Precompute the log-frequency row → FFT bin map (row 0 = top = Nyquist).
    const float sr  = float(snap->sampleRate > 0 ? snap->sampleRate : 48000);
    const float nyq = sr * 0.5f;
    const float fMin = 20.f;
    std::vector<int> rowBin(static_cast<size_t>(rows));
    for (int y = 0; y < rows; ++y) {
        const float frac = float(rows - 1 - y) / float(rows - 1); // bottom→0, top→1
        const float hz   = fMin * std::pow(nyq / fMin, frac);
        int bin = int(hz / nyq * float(numBins - 1));
        rowBin[size_t(y)] = std::clamp(bin, 0, numBins - 1);
    }

    QImage img(cols, rows, QImage::Format_RGB32);
    if (img.isNull()) return {};

    std::vector<std::complex<float>> buf(N);
    std::vector<float> mag(static_cast<size_t>(numBins));

    for (int c = 0; c < cols; ++c) {
        const qint64 start = qint64(c) * hop;
        for (int i = 0; i < N; ++i) {
            const qint64 f = start + i;
            float v = 0.f;
            if (f < frames) {
                v = s[size_t(f * ch)];
                if (ch >= 2) v = (v + s[size_t(f * ch + 1)]) * 0.5f;
            }
            buf[size_t(i)] = std::complex<float>(v * window[size_t(i)], 0.f);
        }
        fft(buf);
        for (int b = 0; b < numBins; ++b)
            mag[size_t(b)] = std::abs(buf[size_t(b)]) / float(N);

        for (int y = 0; y < rows; ++y) {
            const float m  = mag[size_t(rowBin[size_t(y)])];
            const float db = (m > 1e-9f) ? 20.f * std::log10(m) : -90.f;
            reinterpret_cast<QRgb *>(img.scanLine(y))[c] = heat(db);
        }
    }
    return img;
}

} // namespace quewi::ui::spectro
