#pragma once

#include <QAbstractTableModel>
#include <QPointer>

// Full definitions, not forward declarations: QPointer<T> needs the complete
// type to static_cast T* to QObject*, and the inline accessors below force
// that instantiation right here in the header.
#include "core/CueList.h"
#include "mix/MixShow.h"

namespace quewi::mix { class MixCue; }

namespace quewi::ui {

// The DCA cue grid: rows are mix cues, columns are DCAs.
//
// This is the view TheatreMix is actually bought for. Each cell shows what's
// on that DCA for that cue, and — more importantly — how it differs from the
// cue before it, because that's what the operator needs to know at a glance:
// "what does pressing GO change?"
class MixGridModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // What changed in this cell relative to the previous cue. The operator
    // reads these as colours; the meanings follow TheatreMix's convention
    // because that's what the people who'd use this already know.
    enum class CellChange {
        Unchanged,   // same channels as the previous cue — no colour
        Assigned,    // a channel arrives on this DCA — the one you must notice
        Removed,     // everything left this DCA
        Modified,    // some in, some out
    };
    Q_ENUM(CellChange)

    // Custom roles so the delegate can paint without re-deriving anything.
    enum Roles {
        CellChangeRole = Qt::UserRole + 1,
        IsDcaColumnRole,
        DcaNumberRole,
        CueRole,
    };

    explicit MixGridModel(QObject *parent = nullptr);
    ~MixGridModel() override;

    void setCueList(core::CueList *list);
    void setMixShow(mix::MixShow *show);

    core::CueList *cueList() const { return m_list; }
    mix::MixShow  *mixShow() const { return m_show; }

    mix::MixCue *cueAt(int row) const;

    // Columns: 0 = cue number, 1 = cue name, then one per DCA.
    static constexpr int kColNumber = 0;
    static constexpr int kColName   = 1;
    static constexpr int kFixedCols = 2;
    int  dcaForColumn(int column) const;   // 1-based; 0 if not a DCA column

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Editing a cell replaces that DCA's strips for that cue. Accepts a
    // comma-separated list of channel names, strip numbers, or ensemble names
    // — whatever the operator types, resolved against the MixShow.
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    // Human-readable contents of a cell, e.g. "Elphaba, Glinda".
    QString cellText(int row, int dca) const;

signals:
    // A cue's assignments changed through the grid. MainWindow uses this to
    // push a live edit at the console when the edited cue is the active one.
    void cueEdited(quewi::mix::MixCue *cue);

private slots:
    void onListChanged();

private:
    void rebuild();
    CellChange changeFor(int row, int dca) const;

    QPointer<core::CueList> m_list;
    QPointer<mix::MixShow>  m_show;
};

} // namespace quewi::ui
