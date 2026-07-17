#pragma once

#include <QDialog>
#include <QPointer>

class QListWidget;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace quewi::mix { class MixShow; }

namespace quewi::ui {

// Sets up the channels and ensembles a mix show programs against.
//
// This is what makes the DCA grid usable: until a strip is named here, the
// grid can only hold bare numbers, and resolve() drops any strip that isn't a
// registered channel — so an unedited show's grid does nothing. The TheatreMix
// workflow is "define your mics, then program cues", and this is the "define
// your mics" step.
//
// Edits the MixShow directly (like the patch editor, not like a cue — channel
// setup is configuration, not a cue-list action, so it stays off the undo
// stack). The grid listens to MixShow's signals, so changes here show up there
// live.
class ChannelEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChannelEditorDialog(mix::MixShow *show, QWidget *parent = nullptr);
    ~ChannelEditorDialog() override;

private slots:
    void addChannel();
    void removeSelectedChannel();
    void onChannelItemChanged(QTableWidgetItem *item);

    void addEnsemble();
    void renameSelectedEnsemble();
    void removeSelectedEnsemble();
    void onEnsembleSelected();
    void onMemberToggled(QTableWidgetItem *item);

private:
    void reloadChannels();
    void reloadEnsembleList();
    void reloadMembers();
    int  nextFreeStrip() const;
    QString selectedEnsemble() const;

    QPointer<mix::MixShow> m_show;

    QTableWidget *m_channels = nullptr;
    QPushButton  *m_removeChannel = nullptr;

    QListWidget  *m_ensembles = nullptr;
    QPushButton  *m_renameEnsemble = nullptr;
    QPushButton  *m_removeEnsemble = nullptr;
    QTableWidget *m_members = nullptr;   // one row per channel, checkable

    // Guards the itemChanged handlers while we populate the tables, so
    // programmatic fills don't get mistaken for user edits.
    bool m_loading = false;
};

} // namespace quewi::ui
