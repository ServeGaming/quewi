#pragma once

#include "core/Workspace.h"

#include <QObject>
#include <QString>

namespace quewi::cues {

// Polymorphic base for every cue type. Subclasses register with CueRegistry
// at startup (see structure.md §9).
class Cue : public QObject {
    Q_OBJECT
public:
    explicit Cue(QObject *parent = nullptr);
    ~Cue() override;

    core::CueId id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(QString n) { m_name = std::move(n); }

private:
    core::CueId m_id;
    QString m_name;
};

} // namespace quewi::cues
