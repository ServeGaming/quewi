#include "ui/LeaderKey.h"

#include "ui/ShortcutManager.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

namespace quewi::ui {

QKeySequence LeaderKey::defaultKey() { return QKeySequence(Qt::Key_QuoteLeft); }

LeaderKey::LeaderKey(ShortcutManager *shortcuts, QWidget *window)
    : QObject(window), m_shortcuts(shortcuts), m_window(window)
{
    m_action = new QAction(tr("Command menu leader (tap: menu · hold + letter: chord)"), window);
    m_action->setShortcutContext(Qt::WindowShortcut);
    m_action->setAutoRepeat(false);
    window->addAction(m_action);
    if (m_shortcuts)
        m_shortcuts->registerAction(QString::fromLatin1(kShortcutId), m_action->text(), m_action, defaultKey());
    else
        m_action->setShortcut(defaultKey());
    connect(m_action, &QAction::triggered, this, &LeaderKey::pressed);
    qApp->installEventFilter(this);
}

LeaderKey::~LeaderKey()
{
    if (qApp) qApp->removeEventFilter(this);
}

QKeySequence LeaderKey::key() const { return m_action->shortcut(); }

void LeaderKey::resyncFromSettings()
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const auto str = s.value(QStringLiteral("shortcuts/%1").arg(QLatin1String(kShortcutId))).toString();
    const QKeySequence want = str.isEmpty() ? defaultKey() : QKeySequence(str);
    if (want == key()) return;
    if (m_shortcuts) m_shortcuts->setBinding(QString::fromLatin1(kShortcutId), want);
    else             m_action->setShortcut(want);
}

bool LeaderKey::isTextEntry(QWidget *w)
{
    if (!w) return false;
    if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w)
        || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QKeySequenceEdit *>(w))
        return true;
    if (auto *cb = qobject_cast<QComboBox *>(w)) return cb->isEditable();
    // Anything else that says it takes text (an open cell editor, a rich editor).
    return w->testAttribute(Qt::WA_InputMethodEnabled)
        && !w->inherits("QAbstractButton") && !w->inherits("QAbstractItemView");
}

void LeaderKey::pressed()
{
    if (!m_window || QApplication::activeWindow() != m_window) return;
    if (isTextEntry(QApplication::focusWidget())) return;
    if (m_held) return;                     // auto-repeat, or a second press before release
    m_held = true;
    m_chordFired = false;
}

void LeaderKey::release()
{
    const bool tap = m_held && !m_chordFired;
    m_held = false;
    m_chordFired = false;
    if (tap) QTimer::singleShot(0, this, [this] { emit tapped(); });
}

bool LeaderKey::eventFilter(QObject *watched, QEvent *event)
{
    const auto type = event->type();
    if (watched == m_window && type == QEvent::WindowActivate) {
        resyncFromSettings();
        return false;
    }
    if (type == QEvent::WindowDeactivate || type == QEvent::ApplicationStateChange) {
        if (m_held && (watched == m_window || type == QEvent::ApplicationStateChange)) {
            m_held = false;
            m_chordFired = false;
        }
        return false;
    }
    if (!m_held) return false;
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease && type != QEvent::ShortcutOverride) return false;

    auto *ke = static_cast<QKeyEvent *>(event);
    const auto combo = key().isEmpty() ? QKeyCombination(Qt::Key_unknown) : key()[0];
    const bool isLeader = ke->key() == combo.key();

    if (type == QEvent::KeyRelease) {
        if (isLeader && !ke->isAutoRepeat()) release();
        return false;
    }
    if (isLeader) { ke->accept(); return true; }      // held: swallow the repeats

    const auto mods = ke->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    const bool letter = (ke->key() >= Qt::Key_A && ke->key() <= Qt::Key_Z)
                     || (ke->key() >= Qt::Key_0 && ke->key() <= Qt::Key_9);
    if (!letter || mods != Qt::NoModifier) return false;

    // While the leader is down, letters are chords — never the cue list's
    // bare-letter shortcuts (A = new audio cue…) and never typed anywhere.
    ke->accept();
    if (type == QEvent::KeyPress && !ke->isAutoRepeat() && !m_chordFired) {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        if (s.value(QStringLiteral("commandmenu/chordsEnabled"), true).toBool()) {
            m_chordFired = true;        // chords off: the release still opens the menu
            const QChar c = QChar(ke->key()).toUpper();
            QTimer::singleShot(0, this, [this, c] { emit chord(c); });
        }
    }
    return true;
}

} // namespace quewi::ui
