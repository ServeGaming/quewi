#include "ui/LiveEffectDevice.h"

#include "audio/AudioEditorModel.h"
#include "audio/AudioEffect.h"

#include <algorithm>

namespace quewi::ui {

LiveEffectDevice::LiveEffectDevice(QObject *parent) : QIODevice(parent) {}

void LiveEffectDevice::start(const std::vector<float> *dryStereo,
                             audio::AudioEditorTrack *track,
                             int sampleRate, qint64 startFrame) {
    close();
    m_dry        = dryStereo;
    m_track      = track;
    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    m_total      = m_dry ? qint64(m_dry->size() / 2) : 0;
    m_pos        = std::clamp<qint64>(startFrame, 0, std::max<qint64>(0, m_total));

    // Prepare + reset ALL effects (even bypassed ones) so toggling one on
    // mid-playback finds it primed rather than running with stale state.
    if (m_track) {
        for (const auto &fx : m_track->effects()) {
            if (!fx) continue;
            fx->prepare(m_sampleRate);
            fx->reset();
        }
    }
    open(QIODevice::ReadOnly);
}

qint64 LiveEffectDevice::bytesAvailable() const {
    return (m_total - m_pos) * 4 + QIODevice::bytesAvailable();
}

qint64 LiveEffectDevice::readData(char *data, qint64 maxlen) {
    if (!m_dry || m_pos >= m_total) return 0; // end → sink stops

    const qint64 framesWanted = maxlen / 4; // 16-bit stereo = 4 bytes/frame
    if (framesWanted <= 0) return 0;
    const qint64 frames = std::min(framesWanted, m_total - m_pos);
    const size_t n = size_t(frames) * 2;

    if (m_scratch.size() < n) m_scratch.resize(n);
    const float *srcStart = m_dry->data() + size_t(m_pos) * 2;
    std::copy(srcStart, srcStart + n, m_scratch.begin());

    // Apply the active track's enabled effects live. Param changes on the GUI
    // thread are picked up here on the next block — that's the "live update".
    if (m_track) {
        for (const auto &fx : m_track->effects()) {
            if (fx && fx->isEnabled())
                fx->process(m_scratch.data(), int(frames));
        }
    }

    qint16 *out = reinterpret_cast<qint16 *>(data);
    for (size_t i = 0; i < n; ++i)
        out[i] = qint16(std::clamp(m_scratch[i], -1.f, 1.f) * 32767.f);

    m_pos += frames;
    return qint64(frames) * 4;
}

} // namespace quewi::ui
