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
      m_samples(SampleStore::makeRam())
{}
AudioFile::~AudioFile() = default;

double AudioFile::durationSeconds() const
{
    if (m_sampleRate == 0) return 0.0;
    return static_cast<double>(m_frameCount) / static_cast<double>(m_sampleRate);
}

qint64 AudioFile::bytesUsed() const
{
    // Single backing buffer — the published snapshot shares the same store
    // so it doesn't double the cost. For RAM stores capacity (not size) is
    // what's committed; a disk-backed long file is paged by the OS and
    // trimmed as it's used, so it costs ~nothing resident. Peaks are
    // ~kPeakBlock smaller and negligible.
    return m_samples->residentBytes();
}

void AudioFile::clear()
{
    if (m_decoder) {
        m_decoder->stop();
        m_decoder.reset();
    }
    // Detach from any in-flight voice's snapshot — they keep the old
    // backing alive via their shared_ptr; we start fresh.
    m_samples = SampleStore::makeRam();
    m_releasedSamples = 0;
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

void AudioFile::loadFromSamples(std::vector<float> interleaved, int channels, int sampleRate)
{
    clear();
    if (channels <= 0 || sampleRate <= 0) { setState(State::Failed); return; }
    m_channelCount = channels;
    m_sampleRate   = sampleRate;
    m_frameCount   = static_cast<qint64>(interleaved.size()) / channels;
    const size_t n = static_cast<size_t>(m_frameCount) * channels;
    const bool big = qint64(n * sizeof(float)) > SampleStore::diskThresholdBytes();
    m_samples = big ? SampleStore::makeDisk() : SampleStore::makeRam();
    if (!m_samples->reserve(n) || !m_samples->resize(n)) { setState(State::Failed); return; }
    if (n) std::memcpy(m_samples->data(), interleaved.data(), n * sizeof(float));
    buildPeaksIncrementally(m_frameCount);
    publishSnapshot();
    m_lastPublishedFrames = m_frameCount;
    setState(State::Loaded);
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
            // QAudioDecoder::duration() is in MILLISECONDS. It was read as
            // microseconds, so every file reserved ~1/1000 of its size and
            // then grew by copy-and-double — briefly holding the old buffer
            // AND one twice its size at each step (a 3-hour mix peaked well
            // over its 4 GB decoded size), and long files never reached the
            // disk-backed threshold.
            const qint64 totalMs = m_decoder ? m_decoder->duration() : -1;
            size_t reserveSamples;
            if (totalMs > 0) {
                const size_t totalFrames = static_cast<size_t>(
                    (totalMs * static_cast<qint64>(m_sampleRate)) / 1000);
                reserveSamples = (totalFrames * static_cast<size_t>(m_channelCount));
                reserveSamples += reserveSamples / 32;
            } else {
                reserveSamples = static_cast<size_t>(m_sampleRate)
                               * static_cast<size_t>(m_channelCount) * 300u;
            }
            // Long files decode into a memory-mapped cache file instead of
            // RAM: a 3-hour mix is ~4 GB of float, which used to sit in RAM
            // for the life of the show. See SampleStore.
            if (qint64(reserveSamples * sizeof(float)) > SampleStore::diskThresholdBytes())
                m_samples = SampleStore::makeDisk();
            // No room in the cache (disk full, no write access)? Fall back to
            // RAM rather than failing the cue.
            if (m_samples->onDisk() && !m_samples->reserve(reserveSamples))
                m_samples = SampleStore::makeRam();
            if (!m_samples->reserve(reserveSamples)) {
                m_error = tr("Not enough memory to decode this file");
                setState(State::Failed);
                if (m_decoder) m_decoder->stop();
                return;
            }
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
            const size_t need = m_samples->size() + growBy;
            const size_t cap  = std::max(need, m_samples->capacity() * 2);
            auto fresh = (m_samples->onDisk()
                          || qint64(cap * sizeof(float)) > SampleStore::diskThresholdBytes())
                       ? SampleStore::makeDisk() : SampleStore::makeRam();
            if (fresh->onDisk() && !fresh->reserve(cap)) fresh = SampleStore::makeRam();
            if (!fresh->reserve(cap) || !fresh->resize(m_samples->size())) {
                m_error = tr("Not enough memory to decode this file");
                setState(State::Failed);
                if (m_decoder) m_decoder->stop();
                return;
            }
            if (!m_samples->empty())
                std::memcpy(fresh->data(), m_samples->data(),
                            m_samples->size() * sizeof(float));
            m_samples = std::move(fresh);
            m_releasedSamples = 0;
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
        // Publish the VERY FIRST chunk immediately (m_lastPublishedFrames
        // still 0), then every ~2 s after that. Without the first-chunk
        // case a cue fired cold has NO playable snapshot until 2 full
        // seconds of audio has decoded — on a busy GUI thread (which is
        // where QAudioDecoder delivers buffers) that meant a music cue
        // GO'd live would report "still decoding" and play silence for
        // the first beat or two, or not start at all. Publishing on the
        // first buffer lets playback begin within tens of milliseconds.
        if (m_lastPublishedFrames == 0
            || m_frameCount - m_lastPublishedFrames >= publishStride) {
            publishSnapshot();
            m_lastPublishedFrames = m_frameCount;
            releaseDecodedPages();
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
    releaseDecodedPages();
    setState(State::Loaded);
}

void AudioFile::releaseDecodedPages()
{
    // Disk store only: everything decoded and already scanned for peaks can
    // leave the working set now; playback pages back in just what it plays.
    // Only the newly decoded stretch each time — re-releasing the whole file
    // every couple of seconds would walk gigabytes of pages.
    if (!m_samples->onDisk() || m_channelCount <= 0) return;
    const size_t done = static_cast<size_t>(m_peakFramesProcessed)
                      * static_cast<size_t>(m_channelCount);
    if (done <= m_releasedSamples) return;
    m_samples->release(m_releasedSamples, done - m_releasedSamples);
    m_releasedSamples = done;
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
    auto fresh = copyOfSamples();
    if (!fresh) return;
    const int chans = m_channelCount;
    const qint64 frames = m_frameCount;
    float *buf = fresh->data();
    for (qint64 i = 0, j = frames - 1; i < j; ++i, --j) {
        for (int c = 0; c < chans; ++c) {
            std::swap(
                buf[static_cast<size_t>(i) * chans + c],
                buf[static_cast<size_t>(j) * chans + c]);
        }
    }
    m_samples = std::move(fresh);
    m_releasedSamples = 0;
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
    for (float v : static_cast<const SampleStore &>(*m_samples)) {
        const float a = std::fabs(v);
        if (a > peak) peak = a;
    }
    if (peak <= 0.0001f) return;
    const float gain = targetPeak / peak;
    // COW for the same reason as reverseSamples.
    auto fresh = copyOfSamples();
    if (!fresh) return;
    float *p = fresh->data();
    for (size_t i = 0, n = fresh->size(); i < n; ++i) p[i] *= gain;
    m_samples = std::move(fresh);
    m_releasedSamples = 0;
    m_peaks.clear();
    m_peaks.reserve(static_cast<size_t>((m_frameCount / kPeakBlock + 1) * m_channelCount));
    m_peakFramesProcessed = 0;
    buildPeaksIncrementally(m_frameCount);
    publishSnapshot();
    emit stateChanged(m_state);
}

std::shared_ptr<SampleStore> AudioFile::copyOfSamples() const
{
    // A fresh store of the same kind holding a copy — the COW that lets
    // voices still playing the old buffer keep reading stable memory.
    auto fresh = m_samples->onDisk() ? SampleStore::makeDisk() : SampleStore::makeRam();
    const size_t n = m_samples->size();
    if (!fresh->reserve(n) || !fresh->resize(n)) return nullptr;
    if (n) std::memcpy(fresh->data(), m_samples->data(), n * sizeof(float));
    return fresh;
}

void AudioFile::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace quewi::audio
