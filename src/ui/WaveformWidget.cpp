#include "ui/WaveformWidget.h"

#include "audio/AudioFile.h"

#include <QPainter>
#include <QPaintEvent>

namespace quewi::ui {

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(80);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

WaveformWidget::~WaveformWidget() = default;

void WaveformWidget::setAudioFile(std::shared_ptr<audio::AudioFile> file)
{
    if (m_file) disconnect(m_file.get(), nullptr, this, nullptr);
    m_file = std::move(file);
    if (m_file) {
        connect(m_file.get(), &audio::AudioFile::stateChanged,
                this, [this]{ update(); });
    }
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor bg(0x16, 0x18, 0x1D);
    const QColor mid(0x33, 0x37, 0x3F);
    const QColor wave(0x62, 0xB4, 0xFF);
    const QColor textCol(0xA8, 0xAE, 0xBA);

    p.fillRect(rect(), bg);

    if (!m_file || m_file->state() == audio::AudioFile::State::Empty) {
        p.setPen(textCol);
        p.drawText(rect(), Qt::AlignCenter, tr("No file loaded"));
        return;
    }
    if (m_file->state() == audio::AudioFile::State::Loading) {
        p.setPen(textCol);
        p.drawText(rect(), Qt::AlignCenter, tr("Loading…"));
        return;
    }
    if (m_file->state() == audio::AudioFile::State::Failed) {
        p.setPen(QColor(0xFF, 0x5A, 0x5A));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Failed: %1").arg(m_file->errorString()));
        return;
    }

    const auto &peaks = m_file->peaks();
    const int chans = m_file->channelCount();
    if (peaks.empty() || chans <= 0) return;

    const int totalRows = static_cast<int>(peaks.size() / chans);
    const int w = width();
    const int h = height();

    // Centerline
    p.setPen(mid);
    p.drawLine(0, h / 2, w, h / 2);

    p.setPen(wave);
    for (int x = 0; x < w; ++x) {
        const int rowStart = static_cast<int>(static_cast<qint64>(x)     * totalRows / w);
        const int rowEnd   = static_cast<int>(static_cast<qint64>(x + 1) * totalRows / w);
        float peak = 0.0f;
        for (int r = rowStart; r < rowEnd && r < totalRows; ++r) {
            for (int c = 0; c < chans; ++c) {
                const float v = peaks[static_cast<size_t>(r) * static_cast<size_t>(chans)
                                       + static_cast<size_t>(c)];
                if (v > peak) peak = v;
            }
        }
        const int half = static_cast<int>(peak * (h / 2 - 2));
        p.drawLine(x, h / 2 - half, x, h / 2 + half);
    }
}

} // namespace quewi::ui
