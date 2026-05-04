#pragma once

#include <QPointer>
#include <QWidget>
#include <memory>

namespace quewi::audio { class AudioFile; }

namespace quewi::ui {

// Renders the AudioFile's pre-computed peak overview as a min/max
// envelope. Cheap: O(width) per paint, regardless of file length.
class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    ~WaveformWidget() override;

    void setAudioFile(std::shared_ptr<audio::AudioFile> file);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::shared_ptr<audio::AudioFile> m_file;
};

} // namespace quewi::ui
