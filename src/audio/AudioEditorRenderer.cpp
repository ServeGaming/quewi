#include "audio/AudioEditorRenderer.h"
#include "audio/AudioEffect.h"

#include <QFile>
#include <QDataStream>
#include <cmath>
#include <algorithm>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::audio {

// ── Fade curve evaluation ─────────────────────────────────────────────────────

float AudioEditorRenderer::applyFade(float sample, qint64 frameInRegion,
                                     qint64 regionDuration,
                                     const FadeCurve &fadeIn,
                                     const FadeCurve &fadeOut)
{
    float gain = 1.f;

    // Fade-in
    if (fadeIn.durationSamples > 0 && frameInRegion < fadeIn.durationSamples) {
        float t = float(frameInRegion) / float(fadeIn.durationSamples);
        switch (fadeIn.type) {
        case FadeCurve::Linear:     gain *= t; break;
        case FadeCurve::EqualPower: gain *= std::sin(t * 0.5f * M_PIf); break;
        case FadeCurve::SCurve:     gain *= (t*t*(3.f - 2.f*t)); break;
        }
    }

    // Fade-out
    if (fadeOut.durationSamples > 0) {
        qint64 fadeOutStart = regionDuration - fadeOut.durationSamples;
        if (frameInRegion >= fadeOutStart) {
            float t = 1.f - float(frameInRegion - fadeOutStart) / float(fadeOut.durationSamples);
            switch (fadeOut.type) {
            case FadeCurve::Linear:     gain *= t; break;
            case FadeCurve::EqualPower: gain *= std::sin(t * 0.5f * M_PIf); break;
            case FadeCurve::SCurve:     gain *= (t*t*(3.f - 2.f*t)); break;
            }
        }
    }

    return sample * gain;
}

// ── Render ────────────────────────────────────────────────────────────────────

AudioEditorRenderer::AudioEditorRenderer(AudioEditorModel *model, QObject *parent)
    : QObject(parent), m_model(model)
{}

bool AudioEditorRenderer::render(std::vector<float> &outStereo) {
    if (!m_model) { m_error = QStringLiteral("No model"); return false; }

    qint64 totalFrames = m_model->totalDurationSamples();
    if (totalFrames <= 0) totalFrames = 1;

    outStereo.assign(size_t(totalFrames) * 2, 0.f);

    // Determine solo state
    bool anySolo = false;
    for (int ti = 0; ti < m_model->trackCount(); ++ti)
        if (m_model->track(ti)->isSoloed()) { anySolo = true; break; }

    // Per-track scratch buffer: each track's region samples are written
    // here at the same length as the final mix, the effects chain is
    // applied to that buffer, then it's mixed into outStereo. This
    // replaces the old code that mixed regions directly into outStereo
    // and skipped effects entirely (leaving the rack inaudible).
    std::vector<float> trackBuf;

    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        auto *track = m_model->track(ti);
        if (track->isMuted()) continue;
        if (anySolo && !track->isSoloed()) continue;

        const float trackGain = track->volume();

        // Reset scratch for this track. assign() doesn't shrink capacity
        // so the second-and-later iterations don't reallocate.
        trackBuf.assign(size_t(totalFrames) * 2, 0.f);

        // Render each region into the track scratch buffer
        for (const auto &region : track->regions()) {
            if (!region.sourceFile ||
                region.sourceFile->state() != AudioFile::State::Loaded) continue;

            const auto &src = region.sourceFile->samples();
            const int    srcCh = region.sourceFile->channelCount();
            const qint64 srcFrames = region.sourceFile->frameCount();
            const int    srcSr = region.sourceFile->sampleRate();
            const int    dstSr = m_model->sampleRate();
            // Source-frame step per timeline-frame (linear-interp resampler).
            const double rate = (srcSr > 0 && dstSr > 0)
                                ? double(srcSr) / double(dstSr) : 1.0;
            const qint64 srcIn  = region.srcInSamples;
            const qint64 srcOut = (region.srcOutSamples < 0) ? srcFrames
                                                              : region.srcOutSamples;
            const qint64 srcDur = std::max<qint64>(0, srcOut - srcIn);
            // Region length on the timeline grid after SRC.
            const qint64 regionDur = (rate > 0.0)
                ? qint64(double(srcDur) / rate) : srcDur;
            const float  regionGain = std::pow(10.f, region.gainDb / 20.f) * trackGain;
            const qint64 tPos = region.timelinePosSamples;

            for (qint64 f = 0; f < regionDur; ++f) {
                const qint64 tlFrame = tPos + f;
                if (tlFrame < 0 || tlFrame >= totalFrames) continue;

                const double srcF = double(srcIn) + double(f) * rate;
                const qint64 i0 = qint64(srcF);
                const qint64 i1 = std::min<qint64>(i0 + 1, srcFrames - 1);
                const float  frac = float(srcF - double(i0));
                if (i0 < 0 || i0 >= srcFrames) continue;

                float l = 0.f, r = 0.f;
                if (srcCh >= 2) {
                    const float l0 = src[i0 * srcCh + 0];
                    const float l1 = src[i1 * srcCh + 0];
                    const float r0 = src[i0 * srcCh + 1];
                    const float r1 = src[i1 * srcCh + 1];
                    l = l0 + frac * (l1 - l0);
                    r = r0 + frac * (r1 - r0);
                } else if (srcCh == 1) {
                    const float s0 = src[i0];
                    const float s1 = src[i1];
                    l = r = s0 + frac * (s1 - s0);
                }

                const float faded = applyFade(1.f, f, regionDur, region.fadeIn, region.fadeOut);
                trackBuf[tlFrame * 2]     += l * faded * regionGain;
                trackBuf[tlFrame * 2 + 1] += r * faded * regionGain;
            }

            emit progress(int(100.0 * double(ti+1) / double(m_model->trackCount())));
        }

        // Apply this track's effects chain to the scratch buffer in
        // place. Process in moderate chunks so effects with internal
        // state (delay lines, envelope followers) see a realistic
        // streaming workload instead of one giant block. 1024 frames
        // is the same block size the live audio callback runs at on
        // most desktop hosts, so reverbs / delays sound the same in
        // the render as they will at play time.
        constexpr int kFxBlockFrames = 1024;
        const int sampleRate = m_model->sampleRate();
        for (const auto &fx : track->effects()) {
            if (!fx || !fx->isEnabled()) continue;
            fx->prepare(sampleRate);
            fx->reset();
            qint64 done = 0;
            while (done < totalFrames) {
                const int chunk = int(std::min<qint64>(kFxBlockFrames,
                                                       totalFrames - done));
                fx->process(trackBuf.data() + done * 2, chunk);
                done += chunk;
            }
        }

        // Mix this track (with effects applied) into the main output.
        for (size_t i = 0; i < trackBuf.size(); ++i)
            outStereo[i] += trackBuf[i];
    }

    // Clamp to [-1, 1] to prevent clipping artifacts from multiple overlapping regions
    for (auto &s : outStereo) s = std::clamp(s, -1.f, 1.f);

    return true;
}

// ── WAV writer ────────────────────────────────────────────────────────────────

bool AudioEditorRenderer::renderToWav(const QString &path) {
    std::vector<float> stereo;
    if (!render(stereo)) return false;
    return writeWav(path, stereo, m_model->sampleRate());
}

bool AudioEditorRenderer::writeWav(const QString &path,
                                   const std::vector<float> &stereo,
                                   int sampleRate)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = QStringLiteral("Cannot open %1 for writing").arg(path);
        return false;
    }

    const int channels  = 2;
    const int bitDepth  = 24;
    const int byteDepth = bitDepth / 8;
    qint64 numFrames    = qint64(stereo.size()) / channels;
    qint64 dataBytes    = numFrames * channels * byteDepth;

    // WAV / RIFF header
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    auto writeTag = [&](const char *tag) {
        f.write(tag, 4);
    };
    auto write32 = [&](quint32 v) { ds << v; };
    auto write16 = [&](quint16 v) { ds << v; };

    writeTag("RIFF");
    write32(quint32(36 + dataBytes));
    writeTag("WAVE");
    writeTag("fmt ");
    write32(16);               // chunk size
    write16(1);                // PCM
    write16(quint16(channels));
    write32(quint32(sampleRate));
    write32(quint32(sampleRate * channels * byteDepth)); // byte rate
    write16(quint16(channels * byteDepth));              // block align
    write16(quint16(bitDepth));
    writeTag("data");
    write32(quint32(dataBytes));

    // Write 24-bit samples. Scale by 2^23-1 and clamp to the signed
    // 24-bit range: multiplying by 2^23 maps +1.0 → 0x800000, which
    // overflows the signed range (max 0x7FFFFF) and wraps to
    // full-NEGATIVE — an audible click at every positive full-scale
    // peak. Round rather than truncate for correct quantisation.
    constexpr qint32 kMax24 =  8388607;   // 2^23 - 1
    constexpr qint32 kMin24 = -8388608;   // -2^23
    for (float s : stereo) {
        const float clamped = std::clamp(s, -1.f, 1.f);
        const qint32 pcm = std::clamp<qint32>(
            qint32(std::lround(clamped * 8388607.0f)), kMin24, kMax24);
        // Write little-endian 3-byte
        f.putChar(char(pcm & 0xFF));
        f.putChar(char((pcm >> 8) & 0xFF));
        f.putChar(char((pcm >> 16) & 0xFF));
    }

    f.close();
    return true;
}

} // namespace quewi::audio
