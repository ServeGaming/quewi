#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;

namespace quewi::cues { class Cue; }

namespace quewi::ui {

// Bottom bar: shows the next cue prominently and exposes the GO button.
// Engine wiring (actual GO firing) lands in Phase 2+; this is the surface
// the engine plugs into.
class TransportBar : public QWidget {
    Q_OBJECT
public:
    explicit TransportBar(QWidget *parent = nullptr);
    ~TransportBar() override;

public slots:
    void setNextCue(quewi::cues::Cue *cue);
    // Drives the DCA GO button — the second GO that fires the mix (DCA)
    // list against the connected console. `ready` enables it and paints it
    // live; `tooltip` explains the current next cue or why it's disabled.
    // MixView owns the mix state, so the label text is computed there and
    // handed in as a string to keep this bar free of mix types.
    void setDcaGoState(bool ready, const QString &tooltip);

signals:
    void goPressed();
    void dcaGoPressed();
    void panicPressed();
    void pausePressed();
    void fadeAllPressed();

private:
    QPointer<cues::Cue> m_nextCue;
    QLabel      *m_nextLabel = nullptr;
    QPushButton *m_goButton  = nullptr;
    QPushButton *m_dcaGo     = nullptr;
    QPushButton *m_pause     = nullptr;
    QPushButton *m_fadeAll   = nullptr;
    QPushButton *m_panic     = nullptr;
};

} // namespace quewi::ui
