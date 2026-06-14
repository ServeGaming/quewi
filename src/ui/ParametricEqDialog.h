#pragma once

#include <QDialog>
#include <QList>
#include <QPoint>
#include <QPointer>

class QUndoStack;

namespace quewi::audio { class EqEffect; }

namespace quewi::ui {

class LiveAudioScope;

// Visual parametric EQ editor. A live response graph with one draggable
// handle per band sits above a strip of per-band controls (filter type,
// frequency, gain, Q, enable). Band ghost-curves are drawn under the
// composite so the operator can see each band's individual contribution.
//
// Interaction:
//   • Drag a handle horizontally → frequency, vertically → gain
//   • Wheel over a handle → Q (shape narrowness)
//   • Double-click a handle → reset that band to flat (0 dB)
//   • Hover anywhere on the graph → readout of frequency + total dB
class ParametricEqDialog : public QDialog {
    Q_OBJECT
public:
    explicit ParametricEqDialog(audio::EqEffect *eq, QWidget *parent = nullptr);
    ~ParametricEqDialog() override = default;

    // Optional live analyzer — when set and playing, a real-time spectrum is
    // drawn behind the EQ response so you can see where the program is peaking.
    void setScope(LiveAudioScope *scope);

    // Snapshot of all bands — the unit of undo/redo. Public so the dialog's
    // undo command (defined in the .cpp) can restore it.
    struct EqBandState { float freq; float gainDb; float Q; int type; bool enabled; };
    void applyState(const QList<EqBandState> &st);

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
    QPointer<audio::EqEffect> m_eq;
    QPointer<LiveAudioScope>  m_scope;

    // Undo/redo for band edits. m_prevState is the last committed snapshot;
    // maybeCommit() pushes a command whenever the bands differ from it.
    QUndoStack          *m_undo = nullptr;
    QList<EqBandState>   m_prevState;
    void captureState(QList<EqBandState> &out) const;
    void maybeCommit();

    static constexpr int kHandleRadius = 9;

    int   m_dragBand  = -1;
    int   m_hoverBand = -1;
    QPoint m_cursor   {-1, -1};   // last mouse pos in widget coords (-1 if outside)

    QRect m_graphRect;
    static constexpr float kFreqMin = 20.f;
    static constexpr float kFreqMax = 20000.f;
    static constexpr float kGainMin = -24.f;
    static constexpr float kGainMax =  24.f;

    // 6-band palette — runs cool→warm so neighbouring bands are visually
    // distinct on the graph.
    static QColor bandColor(int i);

    int    freqToX (float hz) const;
    float  xToFreq (int x)    const;
    int    gainToY (float dB) const;
    float  yToGain (int y)    const;

    int hitTestBand(const QPoint &p) const;

    void layoutGraph();
    void rebuildBandPanel();
    void updatePanel();

    class QWidget *m_panel = nullptr;
};

} // namespace quewi::ui
