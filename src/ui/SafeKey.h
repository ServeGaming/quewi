#pragma once

#include <QKeySequence>
#include <QObject>
#include <QSet>
#include <QString>

class QKeyEvent;

namespace quewi::ui {

// The "safe key": a key that has to be held down for GO to fire, or for
// cues to be deleted, from this computer's keyboard and mouse (Preferences →
// Show Mode → Safe key). A stray Space or Delete then does nothing.
//
// The key is Shift, Ctrl or Alt, or any other single key (a spare F-key, or
// a foot switch that sends a key). For a modifier, Shift+Space has to reach
// GO even though GO's shortcut is plain Space, so the filter turns
// "safe key + GO's key" into a GO request itself (and the same for Delete).
//
// Remote GO (OSC, HeliOSC) isn't gated: the remote is the deliberate act.
class SafeKey : public QObject {
    Q_OBJECT
public:
    enum class Action { Go, Delete };

    static SafeKey *instance();          // installs its event filter on first use

    // Settings (QSettings "safety/…"), re-read by reload().
    bool    requiredFor(Action a) const { return a == Action::Go ? m_forGo : m_forDelete; }
    QString keyName() const { return m_keyName; }   // "Shift", "Ctrl", "Alt", or a key ("F13")
    void    reload();

    static void save(bool forGo, bool forDelete, const QString &keyName);

    // Held right now? (Always true when the action doesn't need it.)
    bool isHeld() const;
    bool allows(Action a) const { return !requiredFor(a) || isHeld(); }

    // "Hold Shift to GO" — for the status bar when something was blocked.
    QString blockedMessage(Action a) const;

    // The key GO and Delete are bound to right now (MainWindow keeps these
    // current; GO can be rebound in Keyboard shortcuts).
    void setGoKey(const QKeySequence &k)     { m_goKey = k; }
    void setDeleteKey(const QKeySequence &k) { m_deleteKey = k; }

signals:
    // The safe key (a modifier) plus GO's / Delete's key was pressed.
    void goChord();
    void deleteChord();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit SafeKey(QObject *parent = nullptr);
    Qt::KeyboardModifier modifier() const;     // Qt::NoModifier for a plain key
    int  plainKey() const;                     // Qt::Key for a plain key, else 0
    bool chordMatches(const QKeyEvent *e, const QKeySequence &bound) const;

    bool    m_forGo = false;
    bool    m_forDelete = false;
    QString m_keyName = QStringLiteral("Shift");
    QSet<int> m_down;                          // plain keys held now
    QKeySequence m_goKey { Qt::Key_Space };
    QKeySequence m_deleteKey { QKeySequence::Delete };
};

} // namespace quewi::ui
