#include "video/VideoOutputWindow.h"

#include <QApplication>
#include <QAudioOutput>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPixmap>
#include <QScreen>
#include <QUrl>
#include <QVideoWidget>

namespace quewi::video {

VideoOutputWindow::VideoOutputWindow(const VideoVoiceParams &params, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool)
    , m_params(params)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground, params.kind == VideoVoiceParams::Text);
    setStyleSheet(QStringLiteral("background:#000000;"));
    setWindowOpacity(params.opacity);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    switch (params.kind) {
    case VideoVoiceParams::Video: {
        m_videoWidget = new QVideoWidget(this);
        m_videoWidget->setStyleSheet(QStringLiteral("background:#000000;"));
        layout->addWidget(m_videoWidget);

        m_player = new QMediaPlayer(this);
        m_audioOut = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOut);
        m_player->setVideoOutput(m_videoWidget);
        m_player->setSource(QUrl::fromLocalFile(params.filePath));
        if (params.loop) m_player->setLoops(QMediaPlayer::Infinite);
        connect(m_player, &QMediaPlayer::mediaStatusChanged,
                this, [this](QMediaPlayer::MediaStatus s) {
                    onMediaStatusChanged(static_cast<int>(s));
                });
        m_player->play();
        break;
    }
    case VideoVoiceParams::Image: {
        m_imageLabel = new QLabel(this);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setStyleSheet(QStringLiteral("background:#000000;"));
        QPixmap pm(params.filePath);
        if (!pm.isNull()) m_imageLabel->setPixmap(pm);
        m_imageLabel->setScaledContents(false);
        layout->addWidget(m_imageLabel);
        break;
    }
    case VideoVoiceParams::Text: {
        m_textLabel = new QLabel(params.text, this);
        m_textLabel->setAlignment(Qt::AlignCenter);
        m_textLabel->setWordWrap(true);
        QFont f = m_textLabel->font();
        f.setPixelSize(params.fontPixelSize);
        f.setWeight(QFont::DemiBold);
        m_textLabel->setFont(f);
        QPalette pal = m_textLabel->palette();
        pal.setColor(QPalette::WindowText, params.textColor);
        m_textLabel->setPalette(pal);
        m_textLabel->setAttribute(Qt::WA_TranslucentBackground);
        layout->addWidget(m_textLabel);
        break;
    }
    }

    layoutOnScreen(params.screenIndex, params.geometry);
}

VideoOutputWindow::~VideoOutputWindow()
{
    if (m_player) {
        m_player->stop();
    }
}

void VideoOutputWindow::onMediaStatusChanged(int status)
{
    if (status == QMediaPlayer::EndOfMedia && !m_params.loop) {
        emit finished();
    }
}

void VideoOutputWindow::layoutOnScreen(int screenIndex, const QRectF &norm)
{
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return;
    const int idx = std::clamp(screenIndex, 0, static_cast<int>(screens.size()) - 1);
    QScreen *target = screens.value(idx, screens.first());

    const QRect g = target->geometry();
    const int x = g.x() + static_cast<int>(g.width()  * norm.x());
    const int y = g.y() + static_cast<int>(g.height() * norm.y());
    const int w = std::max(1, static_cast<int>(g.width()  * norm.width()));
    const int h = std::max(1, static_cast<int>(g.height() * norm.height()));
    setGeometry(x, y, w, h);
}

} // namespace quewi::video
