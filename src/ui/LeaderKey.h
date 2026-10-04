#pragma once

#include <QKeySequence>
#include <QObject>
#include <QPointer>

class QAction;
class QWidget;

namespace quewi::ui {

class ShortcutManager;

// The command menu's leader key — quewi's one-key "Super", Omarchy-style.
//
//   tap  (press and release)        → tapped()   : the key menu opens
//   hold + letter                   → chord(L)   : jump straight to category
//                                                  L, or run what's pinned to it
//
// Default: ` (backtick). It's a single key nothing else in quewi uses,
// it sits in the top-left corner away from Space (GO) and Esc (Panic),
// and holding it for a chord is natural. Rebindable: it's registered with
// the ShortcutManager as "commandmenu.leader", so Tools → Keyboard
// shortcuts and Preferences → Command menu both change it.
//
// Text-field safety: while a text field has the focus the key types its
// character, and nothing else happens. The leader is a window-level
// shortcut on the main window only — editor windows and dialogs never
// see it.
class LeaderKey : public QObject {
    Q_OBJECT
public:
    LeaderKey(ShortcutManager *shortcuts, QWidget *window);
    ~LeaderKey() override;

    static QKeySequence defaultKey();
    static constexpr const char *kShortcutId = "commandmenu.leader";

    QKeySequence key() const;
    bool isHeld() const { return m_held; }

    // Preferences writes the binding straight to QSettings; the window
    // calls this when it comes back to the front so it takes effect.
    void resyncFromSettings();

    // True for widgets where typing must stay typing.
    static bool isTextEntry(QWidget *w);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void tapped();
    void chord(QChar letter);

private:
    void pressed();
    void release();

    ShortcutManager  *m_shortcuts = nullptr;
    QPointer<QWidget> m_window;
    QAction          *m_action = nullptr;
    bool m_held = false;
    bool m_chordFired = false;
};

} // namespace quewi::ui
