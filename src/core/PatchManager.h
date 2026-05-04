#pragma once

#include <QObject>
#include <QString>
#include <QUuid>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

namespace quewi::core {

// Named, reusable routing destinations the user defines once and references
// from many cues. Each patch belongs to a category (audio output, OSC dest,
// MIDI port, DMX universe, video surface) and carries category-specific
// fields stored as a generic key/value map so persistence stays uniform.
//
// Patches live alongside the workspace and serialise into the show file's
// metadata so they ship with the show.
class PatchManager : public QObject {
    Q_OBJECT
public:
    enum class Category {
        AudioOutput,
        OscDestination,
        MidiPort,
        DmxUniverse,
        VideoSurface,
    };
    Q_ENUM(Category)

    struct Patch {
        QUuid       id;
        Category    category;
        QString     name;
        QVariantMap fields;
    };

    explicit PatchManager(QObject *parent = nullptr);
    ~PatchManager() override;

    QVector<Patch> patchesIn(Category cat) const;
    Patch          patch(const QUuid &id) const;
    bool           contains(const QUuid &id) const;
    QString        nameOf(const QUuid &id) const;

    // CRUD — emits patchesChanged(category) on each.
    QUuid add(Category cat, const QString &name, const QVariantMap &fields = {});
    void  rename(const QUuid &id, const QString &newName);
    void  setFields(const QUuid &id, const QVariantMap &fields);
    void  remove(const QUuid &id);
    void  clear();

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &);

    // Returns a friendly category label (used by the editor).
    static QString categoryLabel(Category c);

signals:
    void patchesChanged(Category);

private:
    QVector<Patch> m_patches;
    Category categoryOrThrow(const QUuid &id) const;
};

} // namespace quewi::core

Q_DECLARE_METATYPE(quewi::core::PatchManager::Category)
