#include "ui/SafeKey.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextEdit>

namespace quewi::ui {

namespace {
QSettings settings() { return QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi")); }

// Typing in a text field: Shift+Space is a capital-letter space there, not GO.
bool typingIn(QObject *o)
{
    auto *w = qobject_cast<QWidget *>(o);
    for (; w; w = w->parentWidget()) {
        if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w)
            || qobject_cast<QPlainTextEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w))
            return true;
    }
    return false;
}
} // namespace

SafeKey *SafeKey::instance()
{
    static SafeKey *s = [] {
        auto *k = new SafeKey(qApp);
        qApp->installEventFilter(k);
        return k;
    }();
    return s;
}

SafeKey::SafeKey(QObject *parent) : QObject(parent) { reload(); }

void SafeKey::reload()
{
    auto s = settings();
    m_forGo     = s.value(QStringLiteral("safety/safeKeyForGo"), false).toBool();
    m_forDelete = s.value(QStringLiteral("safety/safeKeyForDelete"), false).toBool();
    m_keyName   = s.value(QStringLiteral("safety/safeKey"), QStringLiteral("Shift")).toString().trimmed();
    if (m_keyName.isEmpty()) m_keyName = QStringLiteral("Shift");
    m_down.clear();
}

void SafeKey::save(bool forGo, bool forDelete, const QString &keyName)
{
    auto s = settings();
    s.setValue(QStringLiteral("safety/safeKeyForGo"), forGo);
    s.setValue(QStringLiteral("safety/safeKeyForDelete"), forDelete);
    s.setValue(QStringLiteral("safety/safeKey"), keyName);
    instance()->reload();
}

Qt::KeyboardModifier SafeKey::modifier() const
{
    if (m_keyName.compare(QLatin1String("Shift"), Qt::CaseInsensitive) == 0) return Qt::ShiftModifier;
    if (m_keyName.compare(QLatin1String("Ctrl"), Qt::CaseInsensitive) == 0)  return Qt::ControlModifier;
    if (m_keyName.compare(QLatin1String("Alt"), Qt::CaseInsensitive) == 0)   return Qt::AltModifier;
    return Qt::NoModifier;
}

int SafeKey::plainKey() const
{
    if (modifier() != Qt::NoModifier) return 0;
    const QKeySequence seq(m_keyName);
    return seq.isEmpty() ? 0 : seq[0].key();
}

bool SafeKey::isHeld() const
{
    // The live keyboard state, or the modifiers on the event being handled
    // (a Shift+click on GO carries Shift even if the OS state lags).
    if (const auto mod = modifier(); mod != Qt::NoModifier)
        return (QGuiApplication::queryKeyboardModifiers() & mod)
            || (QGuiApplication::keyboardModifiers() & mod);
    return m_down.contains(plainKey());
}

QString SafeKey::blockedMessage(Action a) const
{
    return a == Action::Go
        ? tr("Hold %1 to GO (Preferences → Show Mode → Safe key)").arg(m_keyName)
        : tr("Hold %1 to delete cues (Preferences → Show Mode → Safe key)").arg(m_keyName);
}

bool SafeKey::chordMatches(const QKeyEvent *e, const QKeySequence &bound) const
{
    if (bound.isEmpty()) return false;
    const auto combo = bound[0];
    const auto mods = e->modifiers() & ~Qt::KeypadModifier;
    return e->key() == combo.key() && mods == (combo.keyboardModifiers() | modifier());
}

bool SafeKey::eventFilter(QObject *watched, QEvent *event)
{
    const auto type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease && type != QEvent::ShortcutOverride)
        return false;
    auto *e = static_cast<QKeyEvent *>(event);

    // A plain safe key: remember whether it's down (it may be any key).
    if (const int k = plainKey(); k && e->key() == k && !e->isAutoRepeat()) {
        if (type == QEvent::KeyPress)   m_down.insert(k);
        if (type == QEvent::KeyRelease) m_down.remove(k);
        return false;
    }

    // A modifier safe key + GO's / Delete's key: that's the GO / Delete.
    if (modifier() == Qt::NoModifier || typingIn(watched)) return false;
    const bool go  = m_forGo && chordMatches(e, m_goKey);
    const bool del = m_forDelete && chordMatches(e, m_deleteKey);
    if (!go && !del) return false;
    if (type == QEvent::ShortcutOverride) {      // claim it before any shortcut does
        e->accept();
        return true;
    }
    if (type == QEvent::KeyPress && !e->isAutoRepeat()) {
        if (go)  emit goChord();
        if (del) emit deleteChord();
    }
    return true;
}

} // namespace quewi::ui
