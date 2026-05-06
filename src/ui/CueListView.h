#pragma once

#include <QList>
#include <QPointer>
#include <QTreeView>
#include <QUrl>

class QContextMenuEvent;

namespace quewi::core { class CueListModel; class Workspace; }
namespace quewi::cues { class Cue; }

namespace quewi::ui {

// Wraps a QTreeView with quewi-specific defaults: virtualized rows,
// configurable columns, single-click selection, no expansion arrows
// (groups land in Phase 6 — until then the model is flat).
class CueListView : public QTreeView {
    Q_OBJECT
public:
    explicit CueListView(QWidget *parent = nullptr);
    ~CueListView() override;

    void setWorkspace(core::Workspace *workspace);
    void setModel(QAbstractItemModel *model) override;

    cues::Cue *currentCue() const;
    cues::Cue *nextCue() const;

    // Re-reads QSettings ui/cueColumns/* and updates which optional columns
    // are visible. Called after Preferences saves.
    void applyColumnVisibility();

signals:
    void currentCueChanged(quewi::cues::Cue *cue);
    void goRequested();
    void cueDoubleClicked(quewi::cues::Cue *cue);
    // External drop with file URLs onto the list — MainWindow turns these
    // into the appropriate cue type at the requested row.
    void filesDropped(const QList<QUrl> &urls, int insertRow);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;

private:
    QPointer<core::Workspace> m_workspace;

    // Drag-indicator state — tracked so paintEvent can draw a thicker
    // line than the default 1px QStyle drop indicator. Cleared on drop
    // or dragLeave.
    bool      m_dragActive = false;
    QModelIndex m_dragIndex;
    int       m_dragPos = 0;   // QAbstractItemView::DropIndicatorPosition
};

} // namespace quewi::ui
