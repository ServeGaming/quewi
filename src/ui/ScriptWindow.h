#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace quewi { class GoEngine; }
namespace quewi::core { class Workspace; }
namespace quewi::cues { class Cue; }

class QStackedWidget;

namespace quewi::ui {

class ScriptViewer;
class ScriptPdfView;

// Top-level window hosting the ScriptViewer plus the small toolbar
// (Open / Edit | Follow / status). Lives outside the main window so
// the operator can park it on a second monitor — the SM and the cue
// operator are often separate people.
class ScriptWindow : public QWidget {
    Q_OBJECT
public:
    explicit ScriptWindow(QWidget *parent = nullptr);
    ~ScriptWindow() override;

    void setWorkspace(core::Workspace *ws);
    void setGoEngine(GoEngine *engine);

    // Selected cue from the cue-list view; passed through to the viewer
    // so click-to-bind knows which cue to attach. Pass null to clear.
    void setSelectedCue(const QUuid &cueId);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openScript();
    void clearScript();
    void toggleMode();
    void onCueFired(quewi::cues::Cue *cue);
    void updateStatus();

private:
    QPointer<core::Workspace> m_workspace;
    QPointer<GoEngine>        m_goEngine;
    ScriptViewer             *m_viewer = nullptr;
    ScriptPdfView            *m_pdfView = nullptr;
    QStackedWidget           *m_stack = nullptr;
    QPushButton              *m_modeBtn = nullptr;
    QLabel                   *m_status = nullptr;

    void switchToFormat();
    bool isPdfActive() const;
};

} // namespace quewi::ui
