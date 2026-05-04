#pragma once

#include "core/PatchManager.h"
#include <QDialog>
#include <QPointer>

class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTabWidget;
class QWidget;

namespace quewi::ui {

// Single-window editor for every named patch in the workspace. Tabs split
// the categories; each tab has a list on the left, a form on the right,
// add/remove/duplicate buttons in a toolbar.
class PatchEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit PatchEditorDialog(core::PatchManager *manager, QWidget *parent = nullptr);
    ~PatchEditorDialog() override;

private:
    QWidget *buildTab(core::PatchManager::Category cat);
    QWidget *buildForm (core::PatchManager::Category cat, QListWidget *list);
    void     refreshList(core::PatchManager::Category cat, QListWidget *list);

    QPointer<core::PatchManager> m_manager;
    QTabWidget *m_tabs = nullptr;
};

} // namespace quewi::ui
