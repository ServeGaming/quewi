#pragma once

#include "ui/MatrixSource.h"

#include <QAbstractTableModel>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTimer;
class QUndoStack;

namespace quewi::core { class CueList; class Workspace; }
namespace quewi::osc  { class EosCueLists; class EosFeedback; }

namespace quewi::ui {

// The Matrix List's rows as a table: one row per moment, quewi's cue on the
// left, the desk's cue(s) in the Lights lane, then how they came to be lined
// up, times and notes.
//
// Lining up works from either side, always by moving the LIGHTING cue —
// quewi's order is GO order and the matrix never changes it:
//   * drag a lighting row onto a quewi cue → it sits on that cue's row;
//     between rows → its own row after the quewi cue above;
//   * drag a quewi cue onto a lighting row → that lighting cue moves onto
//     the quewi cue's row.
// Every change is one undo step on the show's undo stack (when given one).
class MatrixTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColState, ColNumber, ColCue, ColLights, ColTime, ColPlaced, ColNotes, ColCount };
    enum Role {
        KindRole = Qt::UserRole,       // int(Row::Kind)
        EdgeRole,                      // QColor: the row's region colour (left edge)
        LinkedRole,                    // bool: something on this row is lined up by hand
    };
    static constexpr const char *kDeskMime  = "application/x-quewi-matrix-desk";
    static constexpr const char *kQuewiMime = "application/x-quewi-matrix-quewi";
    static constexpr const char *kMime = kDeskMime;   // (older name)

    explicit MatrixTableModel(QObject *parent = nullptr);

    void setBuild(const MatrixBuild &b);
    void setLive(const MatrixLive &live);
    void setMatrixList(core::CueList *list) { m_list = list; }
    void setUndoStack(QUndoStack *stack) { m_undo = stack; }
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

    // Where a LIGHTING cue dropped at (row, onto?) lands: With the quewi cue
    // of a quewi row dropped onto; otherwise After the quewi cue above
    // (Start at the top).
    bool dropTarget(int row, bool onto, core::matrix::ManualPlacement::Mode *mode,
                    QUuid *anchor) const;
    // A QUEWI cue dropped onto `row`: the lighting cues there that would move
    // onto its row. Empty (with *why) when that isn't a lining-up: a quewi row
    // (that would be reordering quewi's cues), or a gap.
    std::vector<core::matrix::DeskCue> lightsAt(int row, QString *why = nullptr) const;

    // Hand placements, each one undo step. `text` is the undo step's name.
    void place(const std::vector<core::matrix::DeskCue> &cues,
               core::matrix::ManualPlacement::Mode mode, const QUuid &anchor,
               const QString &text = {});
    void lineUp(const core::matrix::DeskCue &cue, const QUuid &quewiCue);
    void unlink(const std::vector<core::matrix::DeskCue> &cues);
    QString quewiNumber(const QUuid &id) const;

signals:
    void placementsEdited();       // the user changed placements
    void refused(const QString &why);   // a drop that can't be done, and why

private:
    QString stateText(int row) const;
    QColor  rowTint(int row) const;
    QColor  rowEdge(int row) const;
    void    commit(const core::matrix::Config &next, const QString &text);

    MatrixBuild m_build;
    MatrixLive  m_live;
    QPointer<core::CueList> m_list;
    QPointer<QUndoStack> m_undo;
    bool m_locked = false;
};

// Comfortable rows, and the region colour down each live row's left edge.
class MatrixRowDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
};

// A Matrix List's page: what it interleaves and from where, a key to the
// colours and marks, the GO Lights button, and the merged table — standby
// and running quewi cues and the desk's live/next cue highlighted across the
// whole row, following the show as it runs.
//
// GO is quewi's normal GO (the source list is the GO context while this page
// is up). GO Lights (button, Ctrl+Shift+G) fires the desk's NEXT cue in this
// matrix's desk list; it is never fired by clicking a row. Double-click a
// quewi row (or its context menu) moves quewi's standby there.
class MatrixView : public QWidget {
    Q_OBJECT
public:
    explicit MatrixView(QWidget *parent = nullptr);
    ~MatrixView() override;

    void setWorkspace(core::Workspace *ws);
    void setCueList(core::CueList *matrix);
    core::CueList *cueList() const { return m_list; }
    void setDesk(osc::EosCueLists *reader, osc::EosFeedback *feedback);
    // What the lighting-desk setting says (an Eos desk? its name).
    void setDeskInfo(bool eosDesk, const QString &deskName);
    // standby + running quewi cues, polled while visible (desk state comes
    // from the feedback link).
    void setLiveProvider(std::function<MatrixLive()> fn) { m_liveProvider = std::move(fn); }
    void setLocked(bool locked);            // Show Mode: no re-placing
    void setUndoStack(QUndoStack *stack);   // placements become undoable

    const MatrixBuild &build() const { return m_model->build(); }
    MatrixTableModel *model() const { return m_model; }
    QTableView *table() const { return m_table; }
    QString statusText() const { return m_statusText; }
    GoLightsTarget goLightsTarget() const;

    void rebuildNow();
    void pollLive();                        // also run by the timer

signals:
    void standbyRequested(const QUuid &cueId);
    void modified();                        // settings: the show changed
    void statusMessage(const QString &text);
    void goLightsRequested();               // the GO Lights button
    void deskSettingsRequested();           // "Set up the lighting desk…"

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void scheduleRebuild();
    void refreshCombos();
    void watchDeskList();
    void onDeskCues(const QString &list);
    void updateStatus();
    void updateGoLights();
    void followTo(int row);
    void contextMenuAt(const QPoint &pos);
    void editConfig(const std::function<void(core::matrix::Config &)> &fn);
    void pickQuewiFor(const core::matrix::DeskCue &cue);
    void pickLightsFor(const QUuid &quewiCue);
    void restyle();

    QPointer<core::Workspace> m_ws;
    QPointer<core::CueList> m_list;
    QPointer<core::CueList> m_sourceWatched;
    QPointer<osc::EosCueLists> m_reader;
    QPointer<osc::EosFeedback> m_feedback;
    std::function<MatrixLive()> m_liveProvider;
    QList<QMetaObject::Connection> m_listConns;
    bool    m_eosDesk = true;
    QString m_deskName;

    MatrixTableModel *m_model = nullptr;
    QTableView  *m_table = nullptr;
    QStackedWidget *m_body = nullptr;       // table, or the empty state
    QLabel      *m_emptyTitle = nullptr;
    QLabel      *m_emptyText = nullptr;
    QPushButton *m_emptyDesk = nullptr;
    QLabel      *m_title = nullptr;
    QLabel      *m_summary = nullptr;
    QLabel      *m_legend = nullptr;
    QLabel      *m_banner = nullptr;
    QPushButton *m_bannerDesk = nullptr;
    QWidget     *m_bannerRow = nullptr;
    QComboBox   *m_source = nullptr;
    QComboBox   *m_deskList = nullptr;
    QPushButton *m_refresh = nullptr;
    QCheckBox   *m_follow = nullptr;
    QPushButton *m_goLights = nullptr;
    QLabel      *m_goLightsHint = nullptr;
    QTimer      *m_rebuildTimer = nullptr;
    QTimer      *m_liveTimer = nullptr;
    QString      m_statusText;
    int  m_lastStandbyRow = -2;
    int  m_lastDeskRow = -2;
    bool m_locked = false;
    bool m_updatingCombos = false;
};

} // namespace quewi::ui
