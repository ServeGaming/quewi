#pragma once

#include "audio/Vbap.h"
#include "core/PatchManager.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

// Helpers around PatchManager::Category::SpeakerArray patches. The
// patch-fields wire format is JSON-friendly (templateKey + speakers
// array) so it round-trips cleanly through the show file. These
// helpers convert between the storage form and a Vbap-ready list of
// Speaker structs.

namespace quewi::audio {

// Read the speaker list from a SpeakerArray patch. Returns an empty
// list if the id doesn't resolve or the patch isn't a SpeakerArray.
QList<Speaker> readSpeakers(const core::PatchManager *patches,
                            const QUuid &patchId);

// Stored patch fields helper — call this when constructing a new patch
// to package the templateKey + speaker list into the QVariantMap shape
// PatchManager expects.
QVariantMap toPatchFields(const QString &templateKey,
                          const QList<Speaker> &speakers);

// Built-in templates — used by the Speaker Patch dialog (Stage 4) and
// to seed a sensible default when an audio cue first enables Object
// Audio without a patch already in the show. Keys: "stereo", "5.1",
// "7.1", "7.1.4". An empty / unknown key returns Stereo.
QList<Speaker> templateSpeakers(const QString &templateKey);
QStringList    templateKeys();
QString        templateLabel(const QString &templateKey);

} // namespace quewi::audio
