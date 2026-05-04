#include "osc/OscDictionary.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace quewi::osc {

void OscDictionary::add(const Entry &e)
{
    for (auto &cur : m_entries) {
        if (cur.address == e.address) {
            cur = e;
            return;
        }
    }
    m_entries.push_back(e);
}

void OscDictionary::remove(const QString &address)
{
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
        [&](const Entry &e) { return e.address == address; }),
        m_entries.end());
}

std::vector<OscDictionary::Entry> OscDictionary::search(const QString &needle) const
{
    if (needle.isEmpty()) return m_entries;
    std::vector<Entry> out;
    out.reserve(m_entries.size());
    for (const auto &e : m_entries) {
        if (e.address.contains(needle, Qt::CaseInsensitive)
            || e.description.contains(needle, Qt::CaseInsensitive)) {
            out.push_back(e);
        }
    }
    return out;
}

QJsonObject OscDictionary::toJson() const
{
    QJsonObject root;
    for (const auto &e : m_entries) {
        QJsonObject node;
        node.insert(QStringLiteral("TYPE"),        e.typeTags);
        node.insert(QStringLiteral("DESCRIPTION"), e.description);
        root.insert(e.address, node);
    }
    return root;
}

bool OscDictionary::fromJson(const QJsonObject &root)
{
    m_entries.clear();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        const auto node = it.value().toObject();
        Entry e;
        e.address     = it.key();
        e.typeTags    = node.value(QStringLiteral("TYPE")).toString();
        e.description = node.value(QStringLiteral("DESCRIPTION")).toString();
        m_entries.push_back(std::move(e));
    }
    return true;
}

bool OscDictionary::loadFromFile(const QString &path, QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) *errorOut = pe.errorString();
        return false;
    }
    return fromJson(doc.object());
}

bool OscDictionary::saveToFile(const QString &path, QString *errorOut) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    return true;
}

} // namespace quewi::osc
