#pragma once

#include "audio/Vbap.h"

#include <QList>
#include <QPointF>
#include <QWidget>

namespace quewi::ui {

// Top-down stage view for object audio. Renders a circular "stage"
// with the listener at the centre, speakers as small squares around
// the perimeter, and the source as a draggable dot. Click + drag the
// source to change its azimuth (and front/back placement, which the
// widget converts to elevation along the +Y axis if hasHeight is true).
//
// Conventions match audio::Speaker: azimuth 0° = front, +90° = right,
// elevation 0° = ear-level, +90° = directly overhead. The widget
// projects the source onto the unit circle so distance from centre
// always equals 1; clicks outside the dome are clamped.
class StageView : public QWidget {
    Q_OBJECT
public:
    explicit StageView(QWidget *parent = nullptr);

    void setSpeakers(const QList<audio::Speaker> &speakers);
    void setAzimuth(float deg);
    void setElevation(float deg);
    float azimuth()   const { return m_azimuth; }
    float elevation() const { return m_elevation; }

    QSize sizeHint() const override { return QSize(220, 220); }
    QSize minimumSizeHint() const override { return QSize(160, 160); }

signals:
    void positionChanged(float azimuthDeg, float elevationDeg);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void updateFromPoint(const QPointF &p);
    QPointF stageCenter() const;
    qreal   stageRadius() const;
    QPointF positionToPoint(float az, float el) const;

    QList<audio::Speaker> m_speakers;
    float m_azimuth   = 0.0f;
    float m_elevation = 0.0f;
    bool  m_dragging  = false;
};

} // namespace quewi::ui
