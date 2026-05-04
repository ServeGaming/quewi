#pragma once

#include "core/CueList.h"

#include <QAbstractItemModel>
#include <QPointer>

namespace quewi::cues { class Cue; }

namespace quewi::core {

// Bridges a CueList to a QTreeView / QListView. Flat for now (no group
// children); will become hierarchical when group cues land in Phase 6.
class CueListModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column {
        ColumnState = 0,    // colored dot (cue color, or armed indicator)
        ColumnNumber,
        ColumnType,
        ColumnName,
        ColumnPreWait,
        ColumnPostWait,
        ColumnNotes,
        ColumnCount,
    };

    enum Role {
        CueIdRole = Qt::UserRole + 1,
        CuePointerRole,
    };

    explicit CueListModel(QObject *parent = nullptr);
    ~CueListModel() override;

    void setCueList(CueList *list);
    CueList *cueList() const { return m_list.data(); }

    cues::Cue *cueAt(const QModelIndex &index) const;
    QModelIndex indexForCue(const cues::Cue *cue, int column = 0) const;

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList    mimeTypes() const override;
    QMimeData     *mimeData(const QModelIndexList &indexes) const override;

private slots:
    void onAboutToInsert(int row);
    void onInserted(int row);
    void onAboutToRemove(int row);
    void onRemoved(int row);
    void onCueChanged(int row);

private:
    void connectList();
    void disconnectList();

    QPointer<CueList> m_list;
};

} // namespace quewi::core
