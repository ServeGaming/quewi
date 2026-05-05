#pragma once

#include <QPushButton>
#include <QColor>

class QVariantAnimation;

namespace quewi::ui {

// QPushButton with an animated hover and press state. Qt's QSS pseudo-
// states are instant; this widget interpolates background and border
// colours over ~140 ms on Enter/Leave so primary actions feel tactile
// at high refresh rates.
//
// Use sparingly: the GO button, transport controls, and inspector
// "primary" buttons benefit; everything else can stay on QSS.
//
// Set the colour palette via setColors(). Default values match the
// warm dark theme — bgInteractive base, bgRowHover lift, accentSoft
// border.
class AnimatedButton : public QPushButton {
    Q_OBJECT
public:
    explicit AnimatedButton(QWidget *parent = nullptr);
    explicit AnimatedButton(const QString &text, QWidget *parent = nullptr);

    void setColors(const QColor &bgNormal,
                   const QColor &bgHover,
                   const QColor &bgPressed,
                   const QColor &fgColor);
    void setBorderRadius(int r) { m_radius = r; update(); }
    void setBorderColor(const QColor &c) { m_borderColor = c; update(); }
    void setUseBorder(bool b)   { m_useBorder = b; update(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void animateTo(float target);
    QColor lerp(const QColor &a, const QColor &b, float t) const;

    QColor m_bgNormal  { 0x34, 0x30, 0x2C };
    QColor m_bgHover   { 0x40, 0x3A, 0x34 };
    QColor m_bgPressed { 0x2A, 0x28, 0x25 };
    QColor m_fgColor   { 0xE8, 0xE2, 0xD4 };
    QColor m_borderColor{ 0x4A, 0x44, 0x3D };
    int    m_radius    = 6;
    bool   m_useBorder = true;

    float  m_hoverProgress = 0.f;
    bool   m_pressed       = false;
    QVariantAnimation *m_anim = nullptr;
};

} // namespace quewi::ui
