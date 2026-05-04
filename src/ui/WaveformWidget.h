#pragma once

#include <QPointer>
#include <QWidget>
#include <memory>

namespace quewi::audio { class AudioFile; }

namespace quewi::ui {

// Renders the AudioFile's pre-computed peak overview as a min/max
// envelope. Cheap: O(width) per paint, regardless of file length.
//
// Two interactive edit modes:
//   * Trim — drag left/right vertical bars to set trim in / out.
//   * Fade — drag the inside edge of triangular fade overlays on left
//            and right to set fade in / out durations.
//
// Modes are mutually exclusive so the user never overlaps the two
// affordances. Inspector flips the mode via the buttons under the
// waveform.
class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    enum class EditMode { None, Trim, Fade };
    Q_ENUM(EditMode)

    explicit WaveformWidget(QWidget *parent = nullptr);
    ~WaveformWidget() override;

    void setAudioFile(std::shared_ptr<audio::AudioFile> file);
    std::shared_ptr<audio::AudioFile> audioFile() const { return m_file; }

    void setEditMode(EditMode mode);
    EditMode editMode() const { return m_mode; }

    // Set markers in seconds. trimOut == 0 means "to end of file".
    void setMarkers(double trimInSec, double trimOutSec,
                    double fadeInSec, double fadeOutSec);

signals:
    // Emitted live (every drag delta) and again on release.
    void trimInChanged(double seconds);
    void trimOutChanged(double seconds);
    void fadeInChanged(double seconds);
    void fadeOutChanged(double seconds);

    // Emitted only on mouse release — the inspector pushes one undo
    // step at the final position.
    void editingFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum class Handle {
        None, TrimIn, TrimOut, FadeIn, FadeOut,
    };

    Handle hitTest(int x) const;
    int    secondsToPixel(double sec) const;
    double pixelToSeconds(int px) const;
    double effectiveDuration() const;

    std::shared_ptr<audio::AudioFile> m_file;
    EditMode m_mode = EditMode::None;

    double m_trimIn  = 0.0;
    double m_trimOut = 0.0;
    double m_fadeIn  = 0.0;
    double m_fadeOut = 0.0;

    Handle m_dragging = Handle::None;
};

} // namespace quewi::ui
