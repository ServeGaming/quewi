#include "audio/AudioFile.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace quewi::audio {

AudioFile::AudioFile(QObject *parent)
    : QObject(parent),
      m_samples(std::make_shared<std::vector<float>>())
{}
AudioFile::~AudioFile() = default;

double AudioFile::durationSeconds() const
{
    if (m_sampleRate == 0) return 0.0;
    return static_cast<double>(m_frameCount) / static_cast<double>(m_sampleRate);
}

qint64 AudioFile::bytesUsed() const
{
    // Single backing buffer — the published snapshot shares the same
    // shared_ptr<vector<float>> so it doesn't double the cost. Capacity
    // (not size) is what's actually committed; we reserve once based
    // on duration, so capacity == final size for normal files. Peaks
    // are ~kPeakBlock smaller and negligible.
    return static_cast<qint64>(m_samples->capacity()) * qint64(sizeof(float));
}

void AudioFile::clear()
{
    if (m_decoder) {
        m_decoder->stop();
        m_decoder.reset();
    }
    // Detach from any in-flight voice's snapshot — they keep the old
    // backing alive via their shared_ptr; we start fresh.
    m_samples = std::make_shared<std::vector<float>>();
    m_peaks.clear();
    m_peaks.shrink_to_fit();
    m_sampleRate = 0;
    m_channelCount = 0;
    m_frameCount = 0;
    m_peakFramesProcessed = 0;
    m_lastPublishedFrames = 0;
    m_path.clear();
    m_error.clear();
    clearSnapshot();
    setState(State::Empty);
}

std::shared_ptr<const AudioBufferSnapshot> AudioFile::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_publishMutex);
    return m_published;   // shared_ptr copy bumps refcount under the lock
}

void AudioFile::publishSnapshot()
{
    auto snap = std::make_shared<AudioBufferSnapshot>();
    snap->samples      = m_samples;            // shared_ptr copy — no audio data copy
    snap->channelCount = m_channelCount;
    snap->sampleRate   = m_sampleRate;
    snap->frameCount   = m_frameCount;
    std::lock_guard<std::mutex> lock(m_publishMutex);
    m_published = std::move(snap);
}

void AudioFile::clearSnapshot()
{
    std::lock_guard<std::mutex> lock(m_publishMutex);
    m_published.reset();
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
            // Reserve once from the decoder's reported duration so the
            // backing never reallocates during normal decode. ~3% slack
            // covers rounding and any trailing partial buffer the
            // decoder hands us. Falls back to 5 minutes when duration
            // is unknown (rare for local files); the COW path below
            // handles overflow safely if the estimate is short.
            const qint64 totalUs = m_decoder ? m_decoder->duration() : -1;
            size_t reserveSamples;
            if (totalUs > 0) {
                const size_t totalFrames = static_cast<size_t>(
                    (totalUs * static_cast<qint64>(m_sampleRate)) / 1'000'000);
                reserveSamples = (totalFrames * static_cast<size_t>(m_channelCount));
                reserveSamples += reserveSamples / 32;
            } else {
                reserveSamples = static_cast<size_t>(m_sampleRate)
                               * static_cast<size_t>(m_channelCount) * 300u;
            }
            m_samples->reserve(reserveSamples);
        }

        // The Qt decoder yields samples in `fmt.sampleFormat()`. We
        // requested Float; if a backend ignores that, convert.
        const int frames = buf.frameCount();
        const int chans  = m_channelCount;

        const auto growBy = static_cast<size_t>(frames) * static_cast<size_t>(chans);

        // Capacity guard. Resizing past capacity reallocates the
        // vector, which invalidates pointers — including the data
        // pointer that any in-flight published snapshot is reading
        // through on the audio thread. COW into a fresh backing
        // instead so the old snapshot keeps pointing at stable
        // memory until its voices finish.
        if (m_samples->size() + growBy > m_samples->capacity()) {
            auto fresh = std::make_shared<std::vector<float>>();
            const size_t need = m_samples->size() + growBy;
            fresh->reserve(std::max(need, m_samples->capacity() * 2));
            fresh->insert(fresh->end(), m_samples->begin(), m_samples->end());
            m_samples = std::move(fresh);
        }

        const auto oldSize = m_samples->size();
        m_samples->resize(oldSize + growBy);
        float *dst = m_samples->data() + oldSize;

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

    // Progressive snapshot publication. Without this a cue fired
    // mid-decode reads from a stale published snapshot whose
    // frameCount is whatever was published last (possibly 0).
    // Publish every ~2 seconds of audio so a voice can keep playing
    // a freshly-fired huge file without ever seeing "still
    // decoding". Voices refresh their snapshot in the mixer when
    // their readPos approaches the end of the captured one.
    if (m_sampleRate > 0) {
        const qint64 publishStride = qint64(m_sampleRate) * 2;
        if (m_frameCount - m_lastPublishedFrames >= publishStride) {
            publishSnapshot();
            m_lastPublishedFrames = m_frameCount;
        }
    }
}

void AudioFile::buildPeaksIncrementally(qint64 newFramesEnd)
{
    // Generate peaks for any complete kPeakBlock-sized blocks we now have.
    const int chans = m_channelCount;
    if (chans <= 0) return;
    while (m_peakFramesProcessed + kPeakBlock <= newFramesEnd) {
        const float *block = m_samples->data()
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
        const float *block = m_samples->data()
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
    publishSnapshot();   // before Loaded — readers must see a valid snapshot
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
    // COW so any voice currently playing the un-reversed buffer keeps
    // reading stable memory until it finishes. New fires read the
    // reversed copy.
    auto fresh = std::make_shared<std::vector<float>>(*m_samples);
    const int chans = m_channelCount;
    const qint64 frames = m_frameCount;
    auto &buf = *fresh;
    for (qint64 i = 0, j = frames - 1; i < j; ++i, --j) {
        for (int c = 0; c < chans; ++c) {
            std::swap(
                buf[static_cast<size_t>(i) * chans + c],
                buf[static_cast<size_t>(j) * chans + c]);
        }
    }
    m_samples = std::move(fresh);
    // Rebuild peaks from scratch.
    m_peaks.clear();
    m_peaks.reserve(static_cast<size_t>((m_frameCount / kPeakBlock + 1) * m_channelCount));
    m_peakFramesProcessed = 0;
    buildPeaksIncrementally(m_frameCount);
    publishSnapshot();
    emit stateChanged(m_state);
}

void AudioFile::normaliseSamples(float targetPeak)
{
    if (m_state != State::Loaded) return;
    float peak = 0.0f;
    for (float v : *m_samples) {
        const float a = std::fabs(v);
        if (a > peak) peak = a;
    }
    if (peak <= 0.0001f) return;
    const float gain = targetPeak / peak;
    // COW for the same reason as reverseSamples.
    auto fresh = std::make_shared<std::vector<float>>(*m_samples);
    for (float &v : *fresh) v *= gain;
    m_samples = std::move(fresh);
    m_peaks.clear();
    m_peaks.reserve(static_cast<size_t>((m_frameCount / kPeakBlock + 1) * m_channelCount));
    m_peakFramesProcessed = 0;
    buildPeaksIncrementally(m_frameCount);
    publishSnapshot();
    emit stateChanged(m_state);
}

void AudioFile::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace quewi::audio
