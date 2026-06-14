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

    // Position of the playhead marker, in absolute file seconds. A negative
    // value hides it (not playing). Driven by the Inspector from the live
    // preview voice's position.
    void setPlayheadSeconds(double sec);

signals:
    // Emitted live (every drag delta) and again on release.
    void trimInChanged(double seconds);
    void trimOutChanged(double seconds);
    void fadeInChanged(double seconds);
    void fadeOutChanged(double seconds);

    // Left-click or left-drag anywhere that isn't a trim/fade handle — the
    // operator wants to move the playhead there. Carries absolute file
    // seconds. The Inspector forwards this to AudioEngine::seek.
    void seekRequested(double seconds);

    // Emitted only on mouse release — the inspector pushes one undo
    // step at the final position.
    void editingFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum class Handle {
        None, TrimIn, TrimOut, FadeIn, FadeOut,
    };

    Handle hitTest(int x) const;
    int    secondsToPixel(double sec) const;
    double pixelToSeconds(int px) const;
    double effectiveDuration() const;
    // Visible window. Zooming shrinks span; panning slides start/end.
    // span() returns the duration of the visible window in seconds.
    double viewSpan() const;
    // Snap a time to the nearest sample-level zero crossing within
    // ±50 ms. Returns the input unchanged if no crossing was found,
    // or the file isn't fully decoded yet.
    double snapToZeroCrossing(double seconds) const;
    // Clamp the (m_viewStart, m_viewEnd) pair to the file duration
    // and ensure they're sane. Called after any zoom or pan.
    void   clampView();

    std::shared_ptr<audio::AudioFile> m_file;
    EditMode m_mode = EditMode::None;

    double m_trimIn  = 0.0;
    double m_trimOut = 0.0;
    double m_fadeIn  = 0.0;
    double m_fadeOut = 0.0;

    Handle m_dragging = Handle::None;
    // Left-drag scrub state — true between press and release on empty space
    // (no handle grabbed), so mouseMove keeps emitting seekRequested.
    bool   m_seeking = false;
    // Playhead position in absolute file seconds; negative = hidden.
    double m_playhead = -1.0;
    // Press-anchored fine-drag state. When Shift is held during a
    // handle drag, the value moves by 0.1× the cursor delta from
    // press, instead of tracking the cursor 1:1 — useful for sample-
    // accurate trim placement.
    double m_dragPressTime = 0.0;
    double m_dragInitialValue = 0.0;

    // Visible window in seconds. m_viewEnd == 0 means "show full
    // duration", which is the unzoomed default. clampView() turns 0
    // into duration once a real zoom happens.
    double m_viewStart = 0.0;
    double m_viewEnd   = 0.0;

    // Middle-button pan state.
    bool   m_panning = false;
    int    m_panAnchorX = 0;
    double m_panAnchorViewStart = 0.0;
};

} // namespace quewi::ui
