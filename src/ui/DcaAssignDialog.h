#pragma once

#include <QDialog>
#include <QSet>
#include <QStringList>

class QListWidget;

namespace quewi::mix { class MixShow; }

namespace quewi::ui {

// "Who's on this DCA?" — the picker that opens when you double-click a DCA cell
// in the mix grid. Instead of typing channel names or strip numbers, tick the
// mics and ensembles from the show. Reads the current assignment so re-opening
// a cell shows what's already there, and offers a shortcut into the channel
// editor for when the show has nothing to pick yet.
class DcaAssignDialog : public QDialog {
    Q_OBJECT
public:
    DcaAssignDialog(mix::MixShow *show, int dca,
                    const QSet<int> &strips, const QStringList &ensembles,
                    QWidget *parent = nullptr);

    QSet<int>   selectedStrips() const;
    QStringList selectedEnsembles() const;

private:
    void rebuildLists();

    mix::MixShow *m_show = nullptr;
    int           m_dca  = 0;
    QSet<int>     m_strips;      // seed / preserved-across-edit selection
    QStringList   m_ensembles;
    QListWidget  *m_channelList  = nullptr;
    QListWidget  *m_ensembleList = nullptr;
};

} // namespace quewi::ui
