#pragma once

#include <QKeyCombination>
#include <QList>
#include <QObject>

namespace quewi::ui {

// Where soundboard pad keybinds are live, and the system-wide listener that
// makes the widest scope work.
//
//   Board   — only while the soundboard is the page on screen.
//   App     — from any page or window of quewi, while quewi is focused.
//   System  — also while quewi is in the BACKGROUND (a game, Discord, a
//             browser has focus), Voicemod-style.
//
// System scope is a low-level keyboard hook (Windows): it WATCHES keys and
// never swallows them — the focused app still receives every keystroke — and
// it reacts only to keys bound to pads, only while quewi is not the
// foreground app (inside quewi the ordinary Qt shortcuts handle them, so
// nothing fires twice). Keys sent by macro pads (Stream Deck, AutoHotkey) are
// honoured. Not available on macOS/Linux yet; there the widest scope is App.
class GlobalHotkeys : public QObject {
    Q_OBJECT
public:
    enum class Scope { Board = 0, App = 1, System = 2 };

    static GlobalHotkeys *instance();
    static bool systemWideSupported();

    Scope scope() const { return m_scope; }
    void  setScope(Scope s);   // persisted per machine; emits scopeChanged

    // The keys to listen for while quewi is in the background. An empty list
    // removes the hook entirely, so nothing is watched when nothing is bound.
    void setWatchedKeys(const QList<QKeyCombination> &keys);

    // Modifiers that carry no meaning for matching (the keypad flag: Qt's
    // key-sequence editor doesn't record it, so numpad 1 == top-row 1).
    static QKeyCombination normalized(QKeyCombination k);

signals:
    void scopeChanged();
    // A watched key was pressed while quewi was in the background.
    void triggered(QKeyCombination key);

private:
    explicit GlobalHotkeys(QObject *parent);
    ~GlobalHotkeys() override;
    void install();
    void uninstall();

public:
    // Called (queued) from the hook with the raw key; resolves it to Qt
    // key combinations and emits triggered() for a watched one. Public only
    // so the platform hook can reach it.
    void handleNativeKey(quint32 vk, quint32 scanCode, bool extended,
                         Qt::KeyboardModifiers mods);

private:
    Scope                  m_scope = Scope::App;
    QList<QKeyCombination> m_watched;
    bool                   m_installed = false;
};

} // namespace quewi::ui
