#include "core/PatchManager.h"
#include <QJsonValue>
#include <algorithm>

namespace quewi::core {

PatchManager::PatchManager(QObject *parent) : QObject(parent) {}
PatchManager::~PatchManager() = default;

QVector<PatchManager::Patch> PatchManager::patchesIn(Category cat) const {
    QVector<Patch> out;
    for (const auto &p : m_patches) if (p.category == cat) out.push_back(p);
    std::sort(out.begin(), out.end(),
              [](const Patch &a, const Patch &b){ return a.name < b.name; });
    return out;
}

PatchManager::Patch PatchManager::patch(const QUuid &id) const {
    for (const auto &p : m_patches) if (p.id == id) return p;
    return {};
}

bool PatchManager::contains(const QUuid &id) const {
    for (const auto &p : m_patches) if (p.id == id) return true;
    return false;
}

QString PatchManager::nameOf(const QUuid &id) const {
    for (const auto &p : m_patches) if (p.id == id) return p.name;
    return {};
}

QUuid PatchManager::add(Category cat, const QString &name, const QVariantMap &fields) {
    Patch p;
    p.id       = QUuid::createUuid();
    p.category = cat;
    p.name     = name;
    p.fields   = fields;
    m_patches.append(p);
    emit patchesChanged(cat);
    return p.id;
}

void PatchManager::rename(const QUuid &id, const QString &newName) {
    for (auto &p : m_patches) {
        if (p.id == id && p.name != newName) {
            p.name = newName;
            emit patchesChanged(p.category);
            return;
        }
    }
}

void PatchManager::setFields(const QUuid &id, const QVariantMap &fields) {
    for (auto &p : m_patches) {
        if (p.id == id) {
            p.fields = fields;
            emit patchesChanged(p.category);
            return;
        }
    }
}

void PatchManager::remove(const QUuid &id) {
    for (int i = 0; i < m_patches.size(); ++i) {
        if (m_patches[i].id == id) {
            const auto cat = m_patches[i].category;
            m_patches.removeAt(i);
            emit patchesChanged(cat);
            return;
        }
    }
}

void PatchManager::clear() {
    m_patches.clear();
    for (auto cat : {Category::AudioOutput, Category::OscDestination,
                     Category::MidiPort,    Category::DmxUniverse,
                     Category::VideoSurface})
        emit patchesChanged(cat);
}

QJsonObject PatchManager::toJson() const {
    QJsonArray arr;
    for (const auto &p : m_patches) {
        QJsonObject o;
        o[QStringLiteral("id")]       = p.id.toString();
        o[QStringLiteral("category")] = int(p.category);
        o[QStringLiteral("name")]     = p.name;
        o[QStringLiteral("fields")]   = QJsonObject::fromVariantMap(p.fields);
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("patches")] = arr;
    return root;
}

void PatchManager::fromJson(const QJsonObject &root) {
    m_patches.clear();
    const auto arr = root.value(QStringLiteral("patches")).toArray();
    for (const auto &v : arr) {
        const auto o = v.toObject();
        Patch p;
        p.id       = QUuid::fromString(o.value(QStringLiteral("id")).toString());
        p.category = Category(o.value(QStringLiteral("category")).toInt());
        p.name     = o.value(QStringLiteral("name")).toString();
        p.fields   = o.value(QStringLiteral("fields")).toObject().toVariantMap();
        m_patches.append(p);
    }
    for (auto cat : {Category::AudioOutput, Category::OscDestination,
                     Category::MidiPort,    Category::DmxUniverse,
                     Category::VideoSurface})
        emit patchesChanged(cat);
}

QString PatchManager::categoryLabel(Category c) {
    switch (c) {
    case Category::AudioOutput:    return tr("Audio Outputs");
    case Category::OscDestination: return tr("OSC Destinations");
    case Category::MidiPort:       return tr("MIDI Ports");
    case Category::DmxUniverse:    return tr("DMX Universes");
    case Category::VideoSurface:   return tr("Video Surfaces");
    case Category::SpeakerArray:   return tr("Speaker Arrays");
    }
    return {};
}

} // namespace quewi::core
