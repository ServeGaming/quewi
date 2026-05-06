#include "ui/Notifications.h"

namespace quewi::ui {

Notifications &Notifications::instance()
{
    static Notifications inst;
    return inst;
}

Notifications::Notifications(QObject *parent) : QObject(parent) {}

void Notifications::post(Level level, const QString &source, const QString &message)
{
    Entry e;
    e.when    = QDateTime::currentDateTime();
    e.level   = level;
    e.source  = source;
    e.message = message;
    // Cap at 1000 entries — any real show generates fewer than that and
    // we don't want a runaway sender to balloon memory.
    if (m_entries.size() > 1000) m_entries.removeFirst();
    m_entries.append(e);
    emit posted(e);
}

QVector<Notifications::Entry> Notifications::recent(int max) const
{
    if (max <= 0 || max >= m_entries.size()) return m_entries;
    return m_entries.mid(m_entries.size() - max);
}

void Notifications::clear()
{
    m_entries.clear();
    emit cleared();
}

} // namespace quewi::ui
