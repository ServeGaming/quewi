#include "audio/AudioFile.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace quewi::audio {

AudioFile::AudioFile(QObject *parent) : QObject(parent) {}
AudioFile::~AudioFile() = default;

double AudioFile::durationSeconds() const
{
    if (m_sampleRate == 0) return 0.0;
    return static_cast<double>(m_frameCount) / static_cast<double>(m_sampleRate);
}

void AudioFile::clear()
{
    if (m_decoder) {
        m_decoder->stop();
        m_decoder.reset();
    }
    m_samples.clear();
    m_samples.shrink_to_fit();
    m_peaks.clear();
    m_peaks.shrink_to_fit();
    m_sampleRate = 0;
    m_channelCount = 0;
    m_frameCount = 0;
    m_peakFramesProcessed = 0;
    m_path.clear();
    m_error.clear();
    setState(State::Empty);
}

void AudioFile::load(const QString &path)
{
    clear();
    m_path = path;
    setState(State::Loading);

    m_decoder = std::make_unique<QAudioDecoder>(this);

    // Decode to a known canonical format: float32 interleaved at the
    // file's native sample rate and channel count. Some Qt backends
    // accept a "preferred" format and convert on the fly.
    QAudioFormat fmt;
    fmt.setSampleFormat(QAudioFormat::Float);
    m_decoder->setAudioFormat(fmt);

    m_decoder->setSource(QUrl::fromLocalFile(path));

    connect(m_decoder.get(), &QAudioDecoder::bufferReady,
            this, &AudioFile::onBufferReady);
    connect(m_decoder.get(), &QAudioDecoder::finished,
            this, &AudioFile::onFinished);
    connect(m_decoder.get(),
            QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
            this, &AudioFile::onError);

    m_decoder->start();
}

void AudioFile::onBufferReady()
{
    while (m_decoder && m_decoder->bufferAvailable()) {
        const QAudioBuffer buf = m_decoder->read();
        if (!buf.isValid()) continue;

        const auto fmt = buf.format();
        if (m_sampleRate == 0) {
            m_sampleRate   = fmt.sampleRate();
            m_channelCount = fmt.channelCount();
            // Reserve a generous initial buffer (5 minutes) — grows if
            // the file is longer.
            m_samples.reserve(static_cast<size_t>(m_sampleRate) * m_channelCount * 300u);
        }

        // The Qt decoder yields samples in `fmt.sampleFormat()`. We
        // requested Float; if a backend ignores that, convert.
        const int frames = buf.frameCount();
        const int chans  = m_channelCount;

        const auto growBy = static_cast<size_t>(frames) * static_cast<size_t>(chans);
        const auto oldSize = m_samples.size();
        m_samples.resize(oldSize + growBy);
        float *dst = m_samples.data() + oldSize;

        switch (fmt.sampleFormat()) {
        case QAudioFormat::Float: {
            const auto *src = buf.constData<float>();
            std::memcpy(dst, src, growBy * sizeof(float));
            break;
        }
        case QAudioFormat::Int16: {
            const auto *src = buf.constData<qint16>();
            constexpr float scale = 1.0f / 32768.0f;
            for (size_t i = 0; i < growBy; ++i) dst[i] = src[i] * scale;
            break;
        }
        case QAudioFormat::Int32: {
            const auto *src = buf.constData<qint32>();
            constexpr float scale = 1.0f / 2147483648.0f;
            for (size_t i = 0; i < growBy; ++i) dst[i] = static_cast<float>(src[i]) * scale;
            break;
        }
        case QAudioFormat::UInt8: {
            const auto *src = buf.constData<quint8>();
            for (size_t i = 0; i < growBy; ++i)
                dst[i] = (static_cast<float>(src[i]) - 128.0f) / 128.0f;
            break;
        }
        default:
            // Unknown sample format — fill silence rather than corrupt.
            std::memset(dst, 0, growBy * sizeof(float));
            break;
        }

        m_frameCount += frames;
        buildPeaksIncrementally(m_frameCount);
    }
}

void AudioFile::buildPeaksIncrementally(qint64 newFramesEnd)
{
    // Generate peaks for any complete kPeakBlock-sized blocks we now have.
    const int chans = m_channelCount;
    if (chans <= 0) return;
    while (m_peakFramesProcessed + kPeakBlock <= newFramesEnd) {
        const float *block = m_samples.data()
            + static_cast<size_t>(m_peakFramesProcessed) * static_cast<size_t>(chans);
        std::vector<float> peakRow(static_cast<size_t>(chans), 0.0f);
        for (int f = 0; f < kPeakBlock; ++f) {
            for (int c = 0; c < chans; ++c) {
                const float a = std::fabs(block[f * chans + c]);
                if (a > peakRow[c]) peakRow[c] = a;
            }
        }
        m_peaks.insert(m_peaks.end(), peakRow.begin(), peakRow.end());
        m_peakFramesProcessed += kPeakBlock;
    }
}

void AudioFile::onFinished()
{
    // Capture any tail block as a final peak entry.
    if (m_channelCount > 0 && m_peakFramesProcessed < m_frameCount) {
        const int chans = m_channelCount;
        const qint64 tail = m_frameCount - m_peakFramesProcessed;
        std::vector<float> peakRow(static_cast<size_t>(chans), 0.0f);
        const float *block = m_samples.data()
            + static_cast<size_t>(m_peakFramesProcessed) * static_cast<size_t>(chans);
        for (qint64 f = 0; f < tail; ++f) {
            for (int c = 0; c < chans; ++c) {
                const float a = std::fabs(block[f * chans + c]);
                if (a > peakRow[c]) peakRow[c] = a;
            }
        }
        m_peaks.insert(m_peaks.end(), peakRow.begin(), peakRow.end());
        m_peakFramesProcessed = m_frameCount;
    }
    setState(State::Loaded);
}

void AudioFile::onError()
{
    if (m_decoder) m_error = m_decoder->errorString();
    setState(State::Failed);
}

void AudioFile::reverseSamples()
{
    if (m_state != State::Loaded || m_channelCount <= 0 || m_frameCount <= 0) return;
    // Reverse frame-by-frame so per-channel order within each frame
    // stays intact (left stays left, right stays right).
    const int chans = m_channelCount;
    const qint64 frames = m_frameCount;
    for (qint64 i = 0, j = frames - 1; i < j; ++i, --j) {
        for (int c = 0; c < chans; ++c) {
            std::swap(
                m_samples[static_cast<size_t>(i) * chans + c],
                m_samples[static_cast<size_t>(j) * chans + c]);
        }
    }
    // Rebuild peaks from scratch.
    m_peaks.clear();
    m_peaks.reserve(static_cast<size_t>((m_frameCount / kPeakBlock + 1) * m_channelCount));
    m_peakFramesProcessed = 0;
    buildPeaksIncrementally(m_frameCount);
    emit stateChanged(m_state);
}

void AudioFile::normaliseSamples(float targetPeak)
{
    if (m_state != State::Loaded) return;
    float peak = 0.0f;
    for (float v : m_samples) {
        const float a = std::fabs(v);
        if (a > peak) peak = a;
    }
    if (peak <= 0.0001f) return;
    const float gain = targetPeak / peak;
    for (float &v : m_samples) v *= gain;
    m_peaks.clear();
    m_peaks.reserve(static_cast<size_t>((m_frameCount / kPeakBlock + 1) * m_channelCount));
    m_peakFramesProcessed = 0;
    buildPeaksIncrementally(m_frameCount);
    emit stateChanged(m_state);
}

void AudioFile::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace quewi::audio
