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

signals:
    void goPressed();
    void panicPressed();
    void pausePressed();
    void fadeAllPressed();

private:
    QPointer<cues::Cue> m_nextCue;
    QLabel      *m_nextLabel = nullptr;
    QPushButton *m_goButton  = nullptr;
    QPushButton *m_pause     = nullptr;
    QPushButton *m_fadeAll   = nullptr;
    QPushButton *m_panic     = nullptr;
};

} // namespace quewi::ui
