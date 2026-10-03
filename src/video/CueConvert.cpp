#include "video/CueConvert.h"

#include "audio/AudioCue.h"
#include "video/VideoCue.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QStringList>

namespace quewi::video::CueConvert {

namespace {

// The fields every cue has (cues::Cue::toPayload). They describe the cue's
// place in the show, so they come from the cue being converted, not from
// whatever was stored for the other type.
const QStringList kIdentityKeys = {
    QStringLiteral("number"), QStringLiteral("name"), QStringLiteral("preWait"),
    QStringLiteral("postWait"), QStringLiteral("continueMode"), QStringLiteral("notes"),
    QStringLiteral("armed"), QStringLiteral("color"), QStringLiteral("linkedCueId"),
};

void copyIdentity(const QJsonObject &from, QJsonObject &to)
{
    for (const auto &k : kIdentityKeys) {
        if (from.contains(k)) to.insert(k, from.value(k));
        else                  to.remove(k);   // e.g. colour cleared
    }
}

} // namespace

bool isVideoFile(const QString &path)
{
    static const QStringList exts = {
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("m4v"),
        QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("webm"),
        QStringLiteral("wmv"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
    };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

bool canConvertToVideo(const audio::AudioCue &audio)
{
    return !audio.videoOrigin().isEmpty() || isVideoFile(audio.filePath());
}

std::unique_ptr<audio::AudioCue> videoToAudio(const VideoCue &video)
{
    QJsonObject p = video.sound()->toPayload();
    copyIdentity(video.toPayload(), p);
    p.remove(QStringLiteral("durationSeconds"));   // runtime metadata
    if (p.value(QStringLiteral("filePath")).toString().isEmpty())
        p.insert(QStringLiteral("filePath"), video.filePath());
    p.insert(QStringLiteral("videoOrigin"), video.toPayload());

    auto out = std::make_unique<audio::AudioCue>();
    out->fromPayload(p);
    out->setId(video.id());
    return out;
}

std::unique_ptr<VideoCue> audioToVideo(const audio::AudioCue &audio)
{
    const QJsonObject audioPayload = audio.toPayload();
    // Start from the video cue it used to be (picture settings), or from a
    // fresh one for an audio cue that's simply playing a video file.
    QJsonObject p = audio.videoOrigin();
    if (p.isEmpty()) {
        p = VideoCue().toPayload();
        p.insert(QStringLiteral("filePath"), audio.filePath());
    }
    copyIdentity(audioPayload, p);

    // The sound as it is NOW — edits made while it was an audio cue carry over.
    QJsonObject sound = audioPayload;
    sound.remove(QStringLiteral("videoOrigin"));
    sound.remove(QStringLiteral("durationSeconds"));
    p.insert(QStringLiteral("sound"), sound);
    p.insert(QStringLiteral("soundEnabled"), true);
    p.insert(QStringLiteral("loop"), audio.loop());

    auto out = std::make_unique<VideoCue>();
    out->fromPayload(p);
    out->setId(audio.id());
    return out;
}

} // namespace quewi::video::CueConvert
