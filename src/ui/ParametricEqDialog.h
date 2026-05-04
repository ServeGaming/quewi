#pragma once

#include <QDialog>
#include <QPointer>

namespace quewi::audio { class EqEffect; }

namespace quewi::ui {

// Visual parametric EQ editor — a popup graph with one control point per band.
// Drag horizontally to change frequency, vertically to change gain, scroll-wheel
// over a band to change Q. Bands are color-coded; the resulting frequency
// response is plotted as a filled curve.
//
// Owns no state — reads/writes through the EqEffect pointer.
class ParametricEqDialog : public QDialog {
    Q_OBJECT
public:
    explicit ParametricEqDialog(audio::EqEffect *eq, QWidget *parent = nullptr);
    ~ParametricEqDialog() override = default;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QPointer<audio::EqEffect> m_eq;

    // Hit-test radius around band points (px)
    static constexpr int kHandleRadius = 10;

    int   m_dragBand = -1;
    int   m_hoverBand = -1;

    // Geometry
    QRect m_graphRect;
    static constexpr float kFreqMin   = 20.f;
    static constexpr float kFreqMax   = 20000.f;
    static constexpr float kGainMin   = -24.f;
    static constexpr float kGainMax   =  24.f;

    // Band colors — matches the screenshot reference (red/orange/yellow/green/blue/purple)
    static QColor bandColor(int i);

    // Coordinate conversions
    int    freqToX (float hz) const;
    float  xToFreq (int x)    const;
    int    gainToY (float dB) const;
    float  yToGain (int y)    const;

    int hitTestBand(const QPoint &p) const;

    void layoutGraph();
    void rebuildBandPanel();

    // Build the labels under the graph
    class QWidget *m_panel = nullptr;
    void updatePanel();
};

} // namespace quewi::ui
