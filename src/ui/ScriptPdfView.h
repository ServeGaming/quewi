#pragma once

#include <QPdfView>
#include <QPointer>
#include <QUuid>
#include <QWidget>

class QPdfDocument;
class QScrollBar;

namespace quewi::core { class ScriptModel; class Workspace; }

namespace quewi::ui {

// QPdfView subclass for the script-follower. Adds:
//   • An overlay layer that draws coloured cue-annotation tabs at the
//     stored (page, yFraction) positions.
//   • mousePressEvent → bind selected cue to (page, yFraction).
//   • scrollToCue → jump to the page and centre the annotation.
//
// Rendering of the PDF itself is QPdfView's job; we only paint the
// overlay on top via paintEvent.
class ScriptPdfView : public QPdfView {
    Q_OBJECT
public:
    enum class Mode { Edit, Follow };

    explicit ScriptPdfView(QWidget *parent = nullptr);
    ~ScriptPdfView() override;

    void setWorkspace(core::Workspace *ws);
    void setMode(Mode m) { m_mode = m; viewport()->update(); }
    Mode mode() const { return m_mode; }

    void setSelectedCue(const QUuid &cueId) { m_selectedCue = cueId; }
    void setNextCue(const QUuid &cueId);
    void scrollToCue(const QUuid &cueId);

    // Reload the PDF document from the current ScriptModel path.
    void reloadDocument();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onScriptChanged();
    void onAnnotationsChanged();

private:
    // Map a viewport-local point to (page, yFraction).
    bool pointToLocation(const QPoint &viewportPoint,
                         int *pageOut, double *yFracOut) const;
    // Compute viewport-rect for a (page, yFraction) tab. Returns invalid
    // rect if the page isn't currently in view.
    QRect locationToRect(int page, double yFraction) const;

    QPointer<core::Workspace> m_workspace;
    QPdfDocument             *m_doc = nullptr;
    Mode    m_mode = Mode::Edit;
    QUuid   m_selectedCue;
    QUuid   m_runningCue;
    QUuid   m_nextCue;
};

} // namespace quewi::ui
