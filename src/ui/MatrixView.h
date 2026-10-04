#pragma once

#include "ui/MatrixSource.h"

#include <QAbstractTableModel>
#include <QPointer>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableView;
class QTimer;

namespace quewi::core { class CueList; class Workspace; }
namespace quewi::osc  { class EosCueLists; class EosFeedback; }

namespace quewi::ui {

// The Matrix List's rows as a table: one row per moment, quewi's cue on the
// left, the desk's cue(s) in the Lights lane, then times and notes. Drag a
// desk cue onto a quewi cue (or between rows) to place it by hand.
class MatrixTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColState, ColNumber, ColCue, ColLights, ColTime, ColNotes, ColCount };
    static constexpr const char *kMime = "application/x-quewi-matrix-desk";

    explicit MatrixTableModel(QObject *parent = nullptr);

    void setBuild(const MatrixBuild &b);
    void setLive(const MatrixLive &live);
    void setMatrixList(core::CueList *list) { m_list = list; }
    void setLocked(bool locked) { m_locked = locked; }
    const MatrixBuild &build() const { return m_build; }
    const MatrixLive &live() const { return m_live; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    // Where a drop at (row, onto?) lands: With the quewi cue of a quewi row
    // dropped onto; otherwise After the quewi cue above (Start at the top).
    // Returns false if nothing sensible (no quewi cues at all and not Start).
    bool dropTarget(int row, bool onto, core::matrix::ManualPlacement::Mode *mode,
                    QUuid *anchor) const;
    // Place desk cues by hand (the drop, and the context menu / tests).
    void place(const std::vector<core::matrix::DeskCue> &cues,
               core::matrix::ManualPlacement::Mode mode, const QUuid &anchor);

signals:
    void placementsEdited();      // the user changed placements

private:
    QString stateText(int row) const;
    QColor  rowTint(int row) const;

    MatrixBuild m_build;
    MatrixLive  m_live;
    QPointer<core::CueList> m_list;
    bool m_locked = false;
};

// A Matrix List's page: what it interleaves, how fresh the desk's cues are,
// and the merged table — standby and running quewi cues and the desk's
// active/pending cue highlighted, following the show as it runs.
//
// GO is quewi's normal GO (the source list is the GO context while this page
// is up); nothing here fires a desk cue. Clicking a row only selects it;
// "Make standby" (double-click a quewi row, or its context menu) moves
// quewi's standby there.
class MatrixView : public QWidget {
    Q_OBJECT
public:
    explicit MatrixView(QWidget *parent = nullptr);
    ~MatrixView() override;

    void setWorkspace(core::Workspace *ws);
    void setCueList(core::CueList *matrix);
    core::CueList *cueList() const { return m_list; }
    void setDesk(osc::EosCueLists *reader, osc::EosFeedback *feedback);
    // standby + running quewi cues, polled while visible (desk state comes
    // from the feedback link).
    void setLiveProvider(std::function<MatrixLive()> fn) { m_liveProvider = std::move(fn); }
    void setLocked(bool locked);            // Show Mode: no re-placing

    const MatrixBuild &build() const { return m_model->build(); }
    MatrixTableModel *model() const { return m_model; }
    QTableView *table() const { return m_table; }
    QString statusText() const;

    void rebuildNow();
    void pollLive();                        // also run by the timer

signals:
    void standbyRequested(const QUuid &cueId);
    void modified();                        // placements / settings: the show changed
    void statusMessage(const QString &text);

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void scheduleRebuild();
    void refreshCombos();
    void watchDeskList();
    void onDeskCues(const QString &list);
    void updateStatus();
    void followTo(int row);
    void contextMenuAt(const QPoint &pos);
    void editConfig(const std::function<void(core::matrix::Config &)> &fn);

    QPointer<core::Workspace> m_ws;
    QPointer<core::CueList> m_list;
    QPointer<core::CueList> m_sourceWatched;
    QPointer<osc::EosCueLists> m_reader;
    QPointer<osc::EosFeedback> m_feedback;
    std::function<MatrixLive()> m_liveProvider;
    QList<QMetaObject::Connection> m_listConns;

    MatrixTableModel *m_model = nullptr;
    QTableView  *m_table = nullptr;
    QLabel      *m_title = nullptr;
    QComboBox   *m_source = nullptr;
    QComboBox   *m_deskList = nullptr;
    QLabel      *m_status = nullptr;
    QPushButton *m_refresh = nullptr;
    QCheckBox   *m_follow = nullptr;
    QTimer      *m_rebuildTimer = nullptr;
    QTimer      *m_liveTimer = nullptr;
    int  m_lastStandbyRow = -2;
    int  m_lastDeskRow = -2;
    bool m_locked = false;
    bool m_updatingCombos = false;
};

} // namespace quewi::ui
