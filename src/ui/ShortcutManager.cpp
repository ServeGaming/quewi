#include "ui/ShortcutManager.h"

#include <QAction>
#include <QSettings>

namespace quewi::ui {

namespace {
constexpr const char *kSettingsGroup = "shortcuts";

QSettings settings()
{
    return QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
}
} // namespace

ShortcutManager::ShortcutManager(QObject *parent) : QObject(parent) {}
ShortcutManager::~ShortcutManager() = default;

QKeySequence ShortcutManager::loadOverride(const QString &id) const
{
    auto s = settings();
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    const auto str = s.value(id).toString();
    return str.isEmpty() ? QKeySequence() : QKeySequence(str);
}

void ShortcutManager::saveOverride(const QString &id, const QKeySequence &seq)
{
    auto s = settings();
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    if (seq.isEmpty()) s.remove(id);
    else               s.setValue(id, seq.toString());
}

void ShortcutManager::registerAction(const QString &id, const QString &label,
                                     QAction *action, const QKeySequence &defaultSeq)
{
    if (!action) return;
    Binding b;
    b.id = id;
    b.label = label;
    b.action = action;
    b.defaultSeq = defaultSeq;
    const auto override_ = loadOverride(id);
    b.currentSeq = override_.isEmpty() ? defaultSeq : override_;
    action->setShortcut(b.currentSeq);
    m_bindings.insert(id, b);
}

QList<ShortcutManager::Binding> ShortcutManager::bindings() const
{
    auto list = m_bindings.values();
    std::sort(list.begin(), list.end(), [](const Binding &a, const Binding &b) {
        return a.label.localeAwareCompare(b.label) < 0;
    });
    return list;
}

void ShortcutManager::setBinding(const QString &id, const QKeySequence &seq)
{
    auto it = m_bindings.find(id);
    if (it == m_bindings.end()) return;
    it->currentSeq = seq;
    if (it->action) it->action->setShortcut(seq);
    if (seq == it->defaultSeq) saveOverride(id, QKeySequence()); // forget override
    else                       saveOverride(id, seq);
    emit bindingsChanged();
}

void ShortcutManager::resetAll()
{
    auto s = settings();
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    s.remove(QString());
    for (auto &b : m_bindings) {
        b.currentSeq = b.defaultSeq;
        if (b.action) b.action->setShortcut(b.defaultSeq);
    }
    emit bindingsChanged();
}

} // namespace quewi::ui
