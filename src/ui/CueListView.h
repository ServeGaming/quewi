#pragma once

#include <QList>
#include <QPointer>
#include <QTreeView>
#include <QUrl>

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

private:
    QPointer<core::Workspace> m_workspace;
};

} // namespace quewi::ui
