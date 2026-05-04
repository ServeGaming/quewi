#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace quewi::osc {

// Static catalogue of known OSC addresses with their type tags and a
// human-readable description. Used by the cue editor's address auto-
// complete and by the OSC Query server to advertise what the app speaks.
//
// Persistable: a dictionary file is plain JSON, so users can hand-edit
// and share them. The format mirrors OSC-Query's "CONTENTS" tree:
//
//   {
//     "/scene/lights": { "TYPE": "i", "DESCRIPTION": "Lighting scene" },
//     "/audio/master": { "TYPE": "f", "DESCRIPTION": "Master gain dB" }
//   }
class OscDictionary {
public:
    struct Entry {
        QString address;
        QString typeTags;        // e.g. "ifs"
        QString description;
    };

    void   add(const Entry &e);                    // upsert by address
    void   remove(const QString &address);
    void   clear()                  { m_entries.clear(); }
    int    size() const             { return int(m_entries.size()); }
    const std::vector<Entry> &entries() const { return m_entries; }

    // Substring-match against address+description, case-insensitive.
    std::vector<Entry> search(const QString &needle) const;

    // JSON I/O. Round-trips the format above.
    QJsonObject toJson() const;
    bool        fromJson(const QJsonObject &root);

    // File I/O — utf-8, pretty-printed.
    bool        loadFromFile(const QString &path, QString *errorOut = nullptr);
    bool        saveToFile(const QString &path,   QString *errorOut = nullptr) const;

private:
    std::vector<Entry> m_entries;
};

} // namespace quewi::osc
