#pragma once

#include <QAbstractButton>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace quewi::ui {

// One collapsible section of an editor's inspector column: a flat header
// (disclosure arrow, small-caps title, an optional right-hand note) over a
// form body. Click the header to fold it away.
class InspectorSection : public QWidget {
    Q_OBJECT
public:
    explicit InspectorSection(const QString &title, QWidget *parent = nullptr);

    QFormLayout *form() const { return m_form; }
    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    QWidget     *body() const { return m_body; }
    // A readout beside the title ("3 triggers", "−2.00 s").
    void setNote(const QString &note);
    void setExpanded(bool on);
    bool isExpanded() const;

    // A label for the form column, styled like every other.
    QLabel *makeLabel(const QString &text);
    void addRow(const QString &label, QWidget *field);

private:
    class Header;
    Header      *m_header     = nullptr;
    QWidget     *m_body       = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QFormLayout *m_form       = nullptr;
};

// The video editor's inspector: one column, four sections — Clip (In, Out,
// how long it plays, loop), Video (opacity, picture fades), Audio (play
// sound, level, sound fades, the way into the audio editor), Lighting (the
// soundtrack's triggers). The fields are public: the editor window wires
// them to the cue and fills them from it; this is only the furniture.
class VideoInspector : public QWidget {
    Q_OBJECT
public:
    explicit VideoInspector(QWidget *parent = nullptr);

    InspectorSection *clipSection     = nullptr;
    InspectorSection *videoSection    = nullptr;
    InspectorSection *audioSection    = nullptr;
    InspectorSection *lightingSection = nullptr;

    QDoubleSpinBox *inSpin         = nullptr;
    QDoubleSpinBox *outSpin        = nullptr;
    QLabel         *playsForLabel  = nullptr;
    QLabel         *cutsLabel      = nullptr;   // "2 cuts · −3.50 s" or hidden
    QCheckBox      *loopCheck      = nullptr;

    QDoubleSpinBox *opacitySpin    = nullptr;
    QDoubleSpinBox *picFadeInSpin  = nullptr;
    QDoubleSpinBox *picFadeOutSpin = nullptr;

    QCheckBox      *soundCheck     = nullptr;
    QDoubleSpinBox *levelSpin      = nullptr;
    QDoubleSpinBox *sndFadeInSpin  = nullptr;
    QDoubleSpinBox *sndFadeOutSpin = nullptr;
    QPushButton    *editSoundBtn   = nullptr;

    QLabel         *triggersLabel  = nullptr;
    QPushButton    *lightingBtn    = nullptr;
};

} // namespace quewi::ui
