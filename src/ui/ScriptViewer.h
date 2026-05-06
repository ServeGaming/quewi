#pragma once

#include <QPlainTextEdit>
#include <QPointer>
#include <QUuid>

namespace quewi::core { class ScriptModel; class Workspace; }

namespace quewi::ui {

// Stage-manager script viewer with cue annotations. Two modes:
//   • Edit:   click a line to bind it to the currently selected cue.
//   • Follow: read-only; auto-scrolls to keep the line for the most
//             recently fired cue centred, with a coloured highlight.
//
// Annotations are rendered two ways:
//   1. A left-margin "ribbon" (LineNumberArea) draws the cue number
//      next to the line — like a traditional SM script with cue
//      lights drawn in the margin.
//   2. The line itself gets a tinted background (running = green,
//      next = amber) using QPlainTextEdit's extra-selection list.
//
// v0.6.0 renders plain text. The header file deliberately exposes a
// `setBackend` enum slot so PDF and DOCX renderers can drop in later
// without changing the public API.
class ScriptViewer : public QPlainTextEdit {
    Q_OBJECT
public:
    enum class Mode { Edit, Follow };

    explicit ScriptViewer(QWidget *parent = nullptr);
    ~ScriptViewer() override;

    void setWorkspace(core::Workspace *ws);
    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    // Currently-selected cue id; the editor uses it as the bind target
    // when the user clicks a line.
    void setSelectedCue(const QUuid &cueId) { m_selectedCue = cueId; }
    QUuid selectedCue() const { return m_selectedCue; }

    // Scroll so the line for cueId is centred and tinted "running".
    // Triggered by GoEngine::cueFired.
    void scrollToCue(const QUuid &cueId);

    // Set the cue that will fire next; tinted "next" so the operator
    // can read ahead. Pass null QUuid to clear.
    void setNextCue(const QUuid &cueId);

    // Width reserved at the left edge for the gutter ribbon.
    int  gutterWidth() const;
    void paintGutter(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onScriptChanged();
    void onAnnotationsChanged();
    void onUpdateRequest(const QRect &rect, int dy);

private:
    int    lineAtCursorPos(const QPoint &pos) const;
    void   refreshHighlights();
    QString cueLabelForLine(int line) const;

    QPointer<core::Workspace> m_workspace;
    Mode    m_mode = Mode::Edit;
    QUuid   m_selectedCue;
    QUuid   m_runningCue;
    QUuid   m_nextCue;
    QWidget *m_gutter = nullptr;
};

} // namespace quewi::ui
