#pragma once

#include <QObject>
#include <QString>

namespace quewi::show {

// SQLite-backed show file (.quewi). Companion JSON sidecar for
// diff-friendly version control. See structure.md §8.
class ShowFile : public QObject {
    Q_OBJECT
public:
    explicit ShowFile(QObject *parent = nullptr);
    ~ShowFile() override;

    bool open(const QString &path);
    bool save();
};

} // namespace quewi::show
