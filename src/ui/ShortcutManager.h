#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QString>

class QAction;

namespace quewi::ui {

// Central registry for user-rebindable shortcuts. Each registered action
// has a stable string id (e.g. "go", "panic", "cue.new.audio"); the
// manager loads overrides from QSettings on construction and writes
// them back when the user edits a binding via ShortcutsDialog.
//
// Actions register themselves with their *default* sequence; the
// manager either keeps the default or replaces it with the saved override.
class ShortcutManager : public QObject {
    Q_OBJECT
public:
    explicit ShortcutManager(QObject *parent = nullptr);
    ~ShortcutManager() override;

    // id is a stable identifier — used as the key in QSettings.
    // label is shown in the editor UI.
    void registerAction(const QString &id, const QString &label,
                        QAction *action, const QKeySequence &defaultSeq);

    struct Binding {
        QString      id;
        QString      label;
        QPointer<QAction> action;
        QKeySequence defaultSeq;
        QKeySequence currentSeq;
    };
    QList<Binding> bindings() const;

    // Apply a new sequence to the named action and persist.
    void setBinding(const QString &id, const QKeySequence &seq);

    // Reset every registered action to its default and clear overrides.
    void resetAll();

signals:
    void bindingsChanged();

private:
    QKeySequence loadOverride(const QString &id) const;
    void saveOverride(const QString &id, const QKeySequence &seq);

    QHash<QString, Binding> m_bindings;
};

} // namespace quewi::ui
