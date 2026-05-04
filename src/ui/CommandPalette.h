#pragma once

#include <QDialog>
#include <QList>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QAction;
class QMenuBar;

namespace quewi::ui {

// Fuzzy-search every action exposed in the menu bar. Bound to Ctrl/Cmd+K.
// Lists actions on open, filters as the user types, fires the chosen
// action on Enter or click. Closes itself on accept or Esc.
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(QMenuBar *menuBar, QWidget *parent = nullptr);
    ~CommandPalette() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void filter(const QString &query);
    void run(QListWidgetItem *item);

private:
    void collect(QMenuBar *bar);

    struct Entry { QAction *action; QString label; };
    QList<Entry> m_entries;

    QLineEdit   *m_input = nullptr;
    QListWidget *m_list  = nullptr;
};

} // namespace quewi::ui
