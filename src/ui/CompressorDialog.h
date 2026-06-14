#pragma once

#include <QDialog>
#include <QPoint>
#include <QPointer>

class QTimer;

namespace quewi::audio { class CompressorEffect; }

namespace quewi::ui {

class LiveAudioScope;

// Visual compressor editor. A live transfer-curve graph (input level →
// output level) sits above a strip of numeric controls. A gain-reduction
// meter runs down the right edge of the graph and moves during preview
// playback so the operator can see the compressor working.
//
// Interaction on the graph:
//   • Drag the threshold handle horizontally → threshold
//   • Drag the ratio handle (right edge of the curve) vertically → ratio
//   • Wheel anywhere on the graph → knee width
//   • Double-click → reset threshold/ratio/knee/makeup to defaults
// The numeric spin boxes below stay two-way bound to the graph.
class CompressorDialog : public QDialog {
    Q_OBJECT
public:
    explicit CompressorDialog(audio::CompressorEffect *comp, QWidget *parent = nullptr);
    ~CompressorDialog() override = default;

    // Optional live analyzer — when set and playing, the current program
    // level is shown moving along the transfer curve and the GR meter tracks
    // the reduction the curve applies at that level.
    void setScope(LiveAudioScope *scope);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QPointer<audio::CompressorEffect> m_comp;
    QPointer<LiveAudioScope>          m_scope;

    // Graph axis ranges (dBFS). Input spans the usable signal range; output
    // is given headroom above 0 so makeup gain is visible.
    static constexpr float kInMin  = -60.f;
    static constexpr float kInMax  =   0.f;
    static constexpr float kOutMin = -60.f;
    static constexpr float kOutMax =  12.f;
    static constexpr float kMeterRange = 24.f; // GR meter spans 0 … -24 dB
    static constexpr int   kHandleRadius = 8;
    static constexpr int   kMeterWidth   = 16;

    QRect m_graphRect; // plotting area (excludes axis margins + meter)

    enum class Drag { None, Threshold, Ratio };
    Drag   m_drag      = Drag::None;
    int    m_hoverHandle = 0;     // 0 none, 1 threshold, 2 ratio
    QPoint m_cursor    {-1, -1};

    QTimer *m_meterTimer = nullptr;
    float   m_lastMeterDb = 0.f;

    QWidget *m_panel = nullptr;

    // Coord mapping
    int   inToX (float dB) const;
    float xToIn (int x)    const;
    int   outToY(float dB) const;
    float yToOut(int y)    const;

    QPoint thresholdHandlePos() const;
    QPoint ratioHandlePos()     const;
    int    hitTestHandle(const QPoint &p) const;

    void layoutGraph();
    void buildPanel();
    void syncPanel();

    // Derive ratio from the dragged output level at 0 dBFS input.
    void setRatioFromOutputAt0(float outDb);
};

} // namespace quewi::ui
