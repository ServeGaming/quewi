#include "ui/LiveAudioScope.h"

#include <algorithm>
#include <cmath>
#include <complex>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::ui {

namespace {

// Radix-2 Cooley-Tukey FFT, in-place.
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

} // namespace

LiveAudioScope::LiveAudioScope(QObject *parent) : QObject(parent) {}

void LiveAudioScope::setInactive() {
    if (!m_active) return;
    m_active = false;
    emit updated();
}

void LiveAudioScope::analyze(const std::vector<float> &pcm,
                             qint64 frameOffset, int sampleRate) {
    constexpr int N = kFftSize;
    const qint64 totalFrames = qint64(pcm.size() / 2); // interleaved stereo
    if (totalFrames < N) { setInactive(); return; }

    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    const qint64 start = std::clamp<qint64>(frameOffset, 0, totalFrames - N);

    if (int(m_window.size()) != N) {
        m_window.resize(N);
        for (int i = 0; i < N; ++i)
            m_window[size_t(i)] = 0.5f * (1.f - std::cos(2.f * M_PIf * i / float(N - 1)));
    }

    std::vector<std::complex<float>> buf(N);
    float peak = 0.f;
    double sumsq = 0.0;
    for (int i = 0; i < N; ++i) {
        const qint64 f = start + i;
        const float mono = 0.5f * (pcm[size_t(f * 2)] + pcm[size_t(f * 2 + 1)]);
        peak = std::max(peak, std::abs(mono));
        sumsq += double(mono) * double(mono);
        buf[size_t(i)] = std::complex<float>(mono * m_window[size_t(i)], 0.f);
    }

    fft(buf);

    const int bins = N / 2 + 1;
    m_mag.resize(size_t(bins));
    for (int b = 0; b < bins; ++b)
        m_mag[size_t(b)] = std::abs(buf[size_t(b)]) / float(N);

    m_peakDb = (peak > 1e-9f) ? 20.f * std::log10(peak) : -120.f;
    const float rms = std::sqrt(float(sumsq / double(N)));
    m_rmsDb = (rms > 1e-9f) ? 20.f * std::log10(rms) : -120.f;

    m_active = true;
    emit updated();
}

} // namespace quewi::ui
