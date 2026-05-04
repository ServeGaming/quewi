#pragma once

#include <QObject>
#include <QUuid>

namespace quewi::core {

using CueId = QUuid;

// Owns CueLists, Patches, Settings. The root of the show document.
class Workspace : public QObject {
    Q_OBJECT
public:
    explicit Workspace(QObject *parent = nullptr);
    ~Workspace() override;
};

} // namespace quewi::core
