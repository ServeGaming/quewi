#pragma once

#include "cues/Cue.h"

namespace quewi::cues {

// A note in the cue list. Doesn't fire anything. Useful as a section header
// or as the simplest cue we can put in the list while other types are
// stubs.
class MemoCue : public Cue {
    Q_OBJECT
public:
    explicit MemoCue(QObject *parent = nullptr);
    ~MemoCue() override;

    QString typeKey() const override { return QStringLiteral("memo"); }
    QString typeName() const override { return tr("Memo"); }
};

} // namespace quewi::cues
