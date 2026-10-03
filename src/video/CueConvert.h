#pragma once

#include <QString>
#include <memory>

namespace quewi::audio { class AudioCue; }

namespace quewi::video {

class VideoCue;

// Turning a video cue into an audio cue and back — "this song's video isn't
// needed tonight, just play the mix". Both directions keep the cue's
// identity (id, number, name, notes, waits, continue, colour, armed, link)
// so Fade / Start / Stop targets and cross-list links still point at it.
//
// Video → audio: the new audio cue carries the video's sound settings (level,
// fades, effects, editor session…) and remembers the whole video cue in its
// "videoOrigin", so converting back restores the picture settings exactly.
// Audio → video: the audio cue's current sound settings become the video's
// soundtrack (edits made while it was an audio cue are kept).
namespace CueConvert {
    std::unique_ptr<audio::AudioCue> videoToAudio(const VideoCue &video);
    std::unique_ptr<VideoCue>        audioToVideo(const audio::AudioCue &audio);

    // An audio cue can become a video cue if it used to be one, or if its
    // file is a video container (.mp4, .mov, …).
    bool canConvertToVideo(const audio::AudioCue &audio);
    bool isVideoFile(const QString &path);
}

} // namespace quewi::video
