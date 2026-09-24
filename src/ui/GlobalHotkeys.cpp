#include "ui/GlobalHotkeys.h"

#include <QChar>
#include <QCoreApplication>
#include <QSettings>

#include <algorithm>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <QSet>
#endif

namespace quewi::ui {

namespace {

GlobalHotkeys *s_instance = nullptr;
const QString kScopeKey = QStringLiteral("soundboard/keyScope");

#ifdef Q_OS_WIN
HHOOK       s_hook = nullptr;
QSet<DWORD> s_held;   // keys currently down: fire on the press, not on repeats

bool isModifierVk(DWORD vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
    case VK_CAPITAL: case VK_NUMLOCK:
        return true;
    default:
        return false;
    }
}

bool quewiIsForeground()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

Qt::KeyboardModifiers currentModifiers()
{
    Qt::KeyboardModifiers m;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= Qt::ShiftModifier;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= Qt::ControlModifier;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= Qt::AltModifier;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        m |= Qt::MetaModifier;
    return m;
}

// Keys that aren't characters. 0 = not one of these.
int specialQtKey(DWORD vk, bool extended)
{
    if (vk >= VK_F1 && vk <= VK_F24) return Qt::Key_F1 + int(vk - VK_F1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return Qt::Key_0 + int(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_MULTIPLY: return Qt::Key_Asterisk;
    case VK_ADD:      return Qt::Key_Plus;
    case VK_SUBTRACT: return Qt::Key_Minus;
    case VK_DECIMAL:  return Qt::Key_Period;
    case VK_DIVIDE:   return Qt::Key_Slash;
    case VK_SPACE:    return Qt::Key_Space;
    case VK_RETURN:   return extended ? Qt::Key_Enter : Qt::Key_Return;
    case VK_TAB:      return Qt::Key_Tab;
    case VK_BACK:     return Qt::Key_Backspace;
    case VK_ESCAPE:   return Qt::Key_Escape;
    case VK_INSERT:   return Qt::Key_Insert;
    case VK_DELETE:   return Qt::Key_Delete;
    case VK_HOME:     return Qt::Key_Home;
    case VK_END:      return Qt::Key_End;
    case VK_PRIOR:    return Qt::Key_PageUp;
    case VK_NEXT:     return Qt::Key_PageDown;
    case VK_LEFT:     return Qt::Key_Left;
    case VK_RIGHT:    return Qt::Key_Right;
    case VK_UP:       return Qt::Key_Up;
    case VK_DOWN:     return Qt::Key_Down;
    case VK_PAUSE:    return Qt::Key_Pause;
    case VK_SNAPSHOT: return Qt::Key_Print;
    case VK_SCROLL:   return Qt::Key_ScrollLock;
    case VK_MEDIA_PLAY_PAUSE: return Qt::Key_MediaTogglePlayPause;
    case VK_MEDIA_NEXT_TRACK: return Qt::Key_MediaNext;
    case VK_MEDIA_PREV_TRACK: return Qt::Key_MediaPrevious;
    case VK_MEDIA_STOP:       return Qt::Key_MediaStop;
    case VK_VOLUME_MUTE:      return Qt::Key_VolumeMute;
    case VK_VOLUME_UP:        return Qt::Key_VolumeUp;
    case VK_VOLUME_DOWN:      return Qt::Key_VolumeDown;
    default: return 0;
    }
}

// The character a key types (unshifted or shifted) in the foreground app's
// layout, as a Qt key (Qt uses the upper-case code point). 0 = none.
int characterQtKey(DWORD vk, DWORD scanCode, bool shifted)
{
    BYTE state[256] = {};
    if (shifted) state[VK_SHIFT] = 0x80;
    const HKL layout = GetKeyboardLayout(
        GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    wchar_t buf[8] = {};
    // Flag 0x4: don't disturb the keyboard's dead-key state (Win10 1607+), so
    // watching a key can't change what the user's app ends up typing.
    const int n = ToUnicodeEx(vk, scanCode, state, buf, 8, 0x4, layout);
    if (n != 1 || buf[0] < 0x20) return 0;
    return QChar(buf[0]).toUpper().unicode();
}

LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_instance) {
        const auto *k = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        const DWORD vk = k->vkCode;
        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            s_held.remove(vk);
        } else if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                   && !s_held.contains(vk)) {
            s_held.insert(vk);
            // Inside quewi the normal shortcuts handle it. Keep this callback
            // tiny — Windows drops a hook that's slow — and do the work on the
            // event loop.
            if (!isModifierVk(vk) && !quewiIsForeground()) {
                const DWORD sc = k->scanCode;
                const bool ext = (k->flags & LLKHF_EXTENDED) != 0;
                const Qt::KeyboardModifiers mods = currentModifiers();
                QMetaObject::invokeMethod(s_instance, [vk, sc, ext, mods] {
                    if (s_instance) s_instance->handleNativeKey(vk, sc, ext, mods);
                }, Qt::QueuedConnection);
            }
        }
    }
    // Always pass the key on: we watch, we never swallow.
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif

} // namespace

GlobalHotkeys *GlobalHotkeys::instance()
{
    if (!s_instance) s_instance = new GlobalHotkeys(QCoreApplication::instance());
    return s_instance;
}

bool GlobalHotkeys::systemWideSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

GlobalHotkeys::GlobalHotkeys(QObject *parent) : QObject(parent)
{
    const int fallback = int(systemWideSupported() ? Scope::System : Scope::App);
    int v = QSettings().value(kScopeKey, fallback).toInt();
    if (v == int(Scope::System) && !systemWideSupported()) v = int(Scope::App);
    m_scope = Scope(std::clamp(v, 0, 2));
}

GlobalHotkeys::~GlobalHotkeys()
{
    uninstall();
    if (s_instance == this) s_instance = nullptr;
}

void GlobalHotkeys::setScope(Scope s)
{
    if (s == Scope::System && !systemWideSupported()) s = Scope::App;
    if (s == m_scope) return;
    m_scope = s;
    QSettings().setValue(kScopeKey, int(s));
    if (s != Scope::System) setWatchedKeys({});
    emit scopeChanged();
}

QKeyCombination GlobalHotkeys::normalized(QKeyCombination k)
{
    return QKeyCombination(k.keyboardModifiers() & ~Qt::KeypadModifier, k.key());
}

void GlobalHotkeys::setWatchedKeys(const QList<QKeyCombination> &keys)
{
    m_watched.clear();
    for (const auto &k : keys) m_watched.append(normalized(k));
    if (m_watched.isEmpty() || m_scope != Scope::System) uninstall();
    else                                                  install();
}

void GlobalHotkeys::install()
{
#ifdef Q_OS_WIN
    if (m_installed) return;
    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboardProc,
                               GetModuleHandleW(nullptr), 0);
    m_installed = (s_hook != nullptr);
    s_held.clear();
#endif
}

void GlobalHotkeys::uninstall()
{
#ifdef Q_OS_WIN
    if (!m_installed) return;
    if (s_hook) UnhookWindowsHookEx(s_hook);
    s_hook = nullptr;
    m_installed = false;
    s_held.clear();
#endif
}

void GlobalHotkeys::handleNativeKey(quint32 vk, quint32 scanCode, bool extended,
                                    Qt::KeyboardModifiers mods)
{
#ifdef Q_OS_WIN
    if (m_watched.isEmpty()) return;
    // A bound key can be stored as the base key ("1") or as the character
    // Shift makes ("Shift+!"), depending on how it was recorded — try both.
    QList<QKeyCombination> candidates;
    if (const int special = specialQtKey(vk, extended)) {
        candidates << QKeyCombination(mods, Qt::Key(special));
    } else {
        if (const int base = characterQtKey(vk, scanCode, false))
            candidates << QKeyCombination(mods, Qt::Key(base));
        if (mods & Qt::ShiftModifier) {
            if (const int shifted = characterQtKey(vk, scanCode, true)) {
                candidates << QKeyCombination(mods, Qt::Key(shifted))
                           << QKeyCombination(mods & ~Qt::ShiftModifier, Qt::Key(shifted));
            }
        }
    }
    for (const auto &c : candidates) {
        if (m_watched.contains(c)) { emit triggered(c); return; }
    }
#else
    Q_UNUSED(vk) Q_UNUSED(scanCode) Q_UNUSED(extended) Q_UNUSED(mods)
#endif
}

} // namespace quewi::ui
